#include "overlay/service.hpp"

#include "log/log.hpp"
#include "overlay/bounds.hpp"
#include "overlay/render/renderer.hpp"
#include "overlay/session.hpp"
#include "win/native.hpp"

namespace hw {
namespace {

    const char* OverlayPreviewName(OverlayPreview preview) noexcept {
        switch (preview) {
            case OverlayPreview::Overlay:
                return "overlay";
            case OverlayPreview::Live:
                return "live";
            case OverlayPreview::Thumbnail:
                return "thumbnail";
        }
        return "unknown";
    }

} // namespace

void OverlayService::DrainCommands(
  OverlayRenderer& renderer,
  OverlayActiveSession& active,
  SettingsPtr& settingsSnapshot,
  PreviewState& preview) noexcept {
    if (m_settingsDirty.exchange(false, std::memory_order_acq_rel)) {
        settingsSnapshot = LoadSettingsSnapshot(m_settings, settingsSnapshot);
        m_outlineManager.ApplySettings(*settingsSnapshot);
    }

    std::optional<OverlayCmd> outlineCommand;
    {
        std::lock_guard lock(m_mutex);
        outlineCommand = std::move(m_pendingOutlineCommand);
        m_pendingOutlineCommand.reset();
    }
    if (outlineCommand) {
        ApplyCommand(renderer, *outlineCommand, active, settingsSnapshot, preview);
    }

    OverlayCmd cmd;
    while (m_commands.pop(cmd)) {
        ApplyCommand(renderer, cmd, active, settingsSnapshot, preview);
    }
}

void OverlayService::ApplyCommand(OverlayRenderer& renderer,
  const OverlayCmd& cmd,
  OverlayActiveSession& active,
  SettingsPtr& settingsSnapshot,
  PreviewState& preview) noexcept {
    std::visit(
      [&](const auto& value) noexcept {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, CommitInteraction>) {
              const InteractionId activeId = session::Id(active);
              if (value.interactionId == 0 || value.interactionId != activeId) {
                  LOG_WARN("interaction: id={} commit ignored active_id={}", value.interactionId, activeId);
                  return;
              }
              const POINT cursor = m_latestMousePos ? m_latestMousePos->load(std::memory_order_relaxed) : POINT{};
              const vec::i4 finalRawRect = session::ComputeBounds(active, cursor);
              m_latestBounds = finalRawRect;
              const HWND target = session::Target(active);
              LOG_DEBUG("interaction: id={} commit received cursor={} raw_rect={} parked={}",
                value.interactionId,
                vec::i2::FromWin32(cursor),
                finalRawRect,
                preview.parkSubmitted);
              if (preview.mode == OverlayPreview::Overlay) {
                  if (!win::PostMoveWindowToRawRect(target, finalRawRect.ToWin32())) {
                      LOG_WARN("interaction: id={} overlay commit queue failed target={:p}",
                        value.interactionId,
                        reinterpret_cast<void*>(target));
                  }
                  CompleteInteraction(renderer, active, preview);
                  return;
              }
              EnsurePlacementWorker().Submit(PlacementRequest{
                .interactionId = value.interactionId,
                .kind = PlacementKind::Commit,
                .target = target,
                .rawRect = finalRawRect,
              });
              if (preview.captureInProgress) {
                  renderer.CancelSnapshotCapture();
              }
              preview.capturePending = false;
              preview.captureInProgress = false;
              preview.parkPending = false;
              preview.finishing = true;
              session::SetCursor(active);
          } else if constexpr (std::is_same_v<T, CancelInteraction>) {
              const InteractionId activeId = session::Id(active);
              if (value.interactionId == 0 || value.interactionId != activeId) {
                  LOG_WARN("interaction: id={} cancel ignored active_id={}", value.interactionId, activeId);
                  return;
              }
              LOG_DEBUG("interaction: id={} cancel received park_submitted={}", value.interactionId, preview.parkSubmitted);
              if (preview.captureInProgress) {
                  renderer.CancelSnapshotCapture();
              }
              if ((preview.parkSubmitted || preview.livePlacementSubmitted) && m_placementWorker) {
                  m_placementWorker->Submit(PlacementRequest{
                    .interactionId = value.interactionId,
                    .kind = PlacementKind::Restore,
                    .target = session::Target(active),
                    .rawRect = session::OriginalRawRect(active),
                  });
                  preview.capturePending = false;
                  preview.captureInProgress = false;
                  preview.parkPending = false;
                  preview.finishing = true;
              } else {
                  CompleteInteraction(renderer, active, preview);
              }
              session::SetCursor(active);
          } else if constexpr (std::is_same_v<T, UseBuiltInShader>) {
              renderer.UseBuiltInShader(value.generation);
          } else if constexpr (std::is_same_v<T, InstallPixelShader>) {
              renderer.InstallPixelShader(value.bytecode, value.generation);
          } else if constexpr (std::is_same_v<T, BeginDrag>) {
              if (session::IsActive(active)) {
                  LOG_WARN("interaction: id={} begin ignored active_id={}", value.session.interactionId, session::Id(active));
                  return;
              }
              settingsSnapshot = LoadSettingsSnapshot(m_settings, settingsSnapshot);
              renderer.ResetSessionAnimation();
              active = value.session;
              const vec::i4 initialRawRect = session::InitialBounds(value.session, value);
              m_latestBounds = initialRawRect;
              renderer.ClearSnapshot();
              preview.Reset();
              preview.mode = settingsSnapshot->move_preview;
              preview.liveRate = settingsSnapshot->live_preview_rate;
              preview.capturePending = preview.mode == OverlayPreview::Thumbnail;
              if (preview.mode != OverlayPreview::Overlay) {
                  EnsurePlacementWorker();
              }
              LOG_DEBUG("interaction: id={} begin received type={} target={:p} preview={} live_rate={} capture_pending={}",
                value.session.interactionId,
                session::TypeName(SessionType::Drag),
                reinterpret_cast<void*>(value.session.target),
                OverlayPreviewName(preview.mode),
                preview.liveRate,
                preview.capturePending);
              session::SetCursor(active);
          } else if constexpr (std::is_same_v<T, BeginResize>) {
              if (session::IsActive(active)) {
                  LOG_WARN("interaction: id={} begin ignored active_id={}", value.session.interactionId, session::Id(active));
                  return;
              }
              settingsSnapshot = LoadSettingsSnapshot(m_settings, settingsSnapshot);
              renderer.ResetSessionAnimation();
              active = value.session;
              m_latestBounds = value.session.startRect;
              renderer.ClearSnapshot();
              preview.Reset();
              preview.mode = settingsSnapshot->resize_preview;
              preview.liveRate = settingsSnapshot->live_preview_rate;
              preview.capturePending = preview.mode == OverlayPreview::Thumbnail;
              if (preview.mode != OverlayPreview::Overlay) {
                  EnsurePlacementWorker();
              }
              LOG_DEBUG("interaction: id={} begin received type={} target={:p} preview={} live_rate={} capture_pending={}",
                value.session.interactionId,
                session::TypeName(SessionType::Resize),
                reinterpret_cast<void*>(value.session.target),
                OverlayPreviewName(preview.mode),
                preview.liveRate,
                preview.capturePending);
              session::SetCursor(active);
          }
      },
      cmd);
}

} // namespace hw
