#include "overlay/service.hpp"

#include "log/log.hpp"
#include "overlay/render/renderer.hpp"
#include "overlay/session.hpp"

namespace hw {
namespace {

const char* PlacementKindName(PlacementKind kind) noexcept {
    switch (kind) {
        case PlacementKind::Park:
            return "park";
        case PlacementKind::Live:
            return "live";
        case PlacementKind::Commit:
            return "commit";
        case PlacementKind::Restore:
            return "restore";
    }
    return "unknown";
}

} // namespace

void OverlayService::PublishPlacementResult(const PlacementResult& result) noexcept {
    if (!m_placementResults.push(result)) {
        LOG_ERROR("interaction: id={} placement result queue full kind={}", result.interactionId, static_cast<int>(result.kind));
        return;
    }
    m_cv.notify_one();
}

PlacementWorker& OverlayService::EnsurePlacementWorker() {
    if (!m_placementWorker) {
        m_placementWorker = std::make_unique<PlacementWorker>([this](const PlacementResult& result) { PublishPlacementResult(result); });
        LOG_DEBUG("placement_worker: started");
    }
    return *m_placementWorker;
}

void OverlayService::RetirePlacementWorkerIfUnused(const OverlayActiveSession& active, const Settings& settings) noexcept {
    const bool placementWorkerRequired =
      settings.move_preview != OverlayPreview::Overlay || settings.resize_preview != OverlayPreview::Overlay;
    if (m_placementWorker && !session::IsActive(active) && !placementWorkerRequired) {
        m_placementWorker->WaitIdle();
        m_placementWorker.reset();
        LOG_DEBUG("placement_worker: stopped");
    }
}

void OverlayService::DrainPlacementResults(
  OverlayRenderer& renderer,
  OverlayActiveSession& active,
  PreviewState& preview) noexcept {
    PlacementResult result;
    while (m_placementResults.pop(result)) {
        const InteractionId activeId = session::Id(active);
        if (result.interactionId == 0 || result.interactionId != activeId) {
            LOG_DEBUG("interaction: id={} stale placement result ignored kind={} active_id={}",
              result.interactionId,
              PlacementKindName(result.kind),
              activeId);
            continue;
        }

        if (result.kind != PlacementKind::Live) {
            LOG_DEBUG("interaction: id={} placement completed kind={} success={} error={}",
              result.interactionId,
              PlacementKindName(result.kind),
              result.success,
              result.error);
        }
        if (result.kind == PlacementKind::Park) {
            LOG_DEBUG("interaction: id={} park placement target={:p} requested_raw_rect={} actual_available={} actual_raw_rect={}",
              result.interactionId,
              reinterpret_cast<void*>(result.target),
              result.rawRect,
              result.actualRawRectAvailable,
              result.actualRawRect);
        }

        switch (result.kind) {
            case PlacementKind::Park:
                if (!result.success && !preview.finishing) {
                    renderer.ClearSnapshot();
                    preview.parkSubmitted = false;
                }
                break;
            case PlacementKind::Live:
                break;
            case PlacementKind::Commit:
                if (!result.success) {
                    EnsurePlacementWorker().Submit(PlacementRequest{
                      .interactionId = result.interactionId,
                      .kind = PlacementKind::Restore,
                      .target = session::Target(active),
                      .rawRect = session::OriginalRawRect(active),
                    });
                    break;
                }
                renderer.ClearSnapshot();
                active = std::monostate{};
                preview.Reset();
                session::SetCursor(active);
                break;
            case PlacementKind::Restore:
                renderer.ClearSnapshot();
                active = std::monostate{};
                preview.Reset();
                session::SetCursor(active);
                break;
        }
    }
}

void OverlayService::RestoreBeforeTeardown(const OverlayActiveSession& active, PreviewState& preview) noexcept {
    const InteractionId interactionId = session::Id(active);
    const bool placementChanged = preview.parkSubmitted || preview.livePlacementSubmitted;
    if (interactionId != 0 && placementChanged && m_placementWorker) {
        m_placementWorker->Submit(PlacementRequest{
          .interactionId = interactionId,
          .kind = PlacementKind::Restore,
          .target = session::Target(active),
          .rawRect = session::OriginalRawRect(active),
        });
        m_placementWorker->WaitIdle();
    }

    PlacementResult ignored;
    while (m_placementResults.pop(ignored)) {
    }
    preview.Reset();
}

} // namespace hw
