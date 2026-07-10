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

PlacementWorker& OverlayService::EnsurePlacementWorker(const DebugSettings& debug) {
    if (!m_placementWorker) {
        m_placementWorker = std::make_unique<PlacementWorker>([this](const PlacementResult& result) { PublishPlacementResult(result); });
        if (debug.enabled(DebugFlag::WindowPlacement)) {
            LOG_DEBUG("placement_worker: started");
        }
    }
    return *m_placementWorker;
}

void OverlayService::RetirePlacementWorkerIfUnused(const OverlayActiveSession& active, const Settings& settings) noexcept {
    const bool placementWorkerRequired =
      settings.move_preview != OverlayPreview::Overlay || settings.resize_preview != OverlayPreview::Overlay;
    if (m_placementWorker && !session::IsActive(active) && !placementWorkerRequired) {
        m_placementWorker->WaitIdle();
        m_placementWorker.reset();
        if (settings.debug.enabled(DebugFlag::WindowPlacement)) {
            LOG_DEBUG("placement_worker: stopped");
        }
    }
}

void OverlayService::DrainPlacementResults(
  OverlayRenderer& renderer,
  OverlayActiveSession& active,
  PreviewState& preview,
  const DebugSettings& debug) noexcept {
    PlacementResult result;
    while (m_placementResults.pop(result)) {
        const InteractionId activeId = session::Id(active);
        if (result.interactionId == 0 || result.interactionId != activeId) {
            if (debug.enabled(DebugFlag::WindowPlacement)) {
                LOG_DEBUG("interaction: id={} stale placement result ignored kind={} active_id={}",
                  result.interactionId,
                  PlacementKindName(result.kind),
                  activeId);
            }
            continue;
        }

        if (result.kind != PlacementKind::Live) {
            if (debug.enabled(DebugFlag::WindowPlacement)) {
                LOG_DEBUG("interaction: id={} placement completed kind={}",
                  result.interactionId,
                  PlacementKindName(result.kind));
            }
        }

        switch (result.kind) {
            case PlacementKind::Park:
            case PlacementKind::Live:
                break;
            case PlacementKind::Commit:
                CompleteInteraction(renderer, active, preview);
                break;
            case PlacementKind::Restore:
                CompleteInteraction(renderer, active, preview);
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
}

void OverlayService::CompleteInteraction(
  OverlayRenderer& renderer,
  OverlayActiveSession& active,
  PreviewState& preview) noexcept {
    renderer.ClearSnapshot();
    active = std::monostate{};
    preview.Reset();
    session::SetCursor(active);
    m_interactionReserved.store(false, std::memory_order_release);
}

} // namespace hw
