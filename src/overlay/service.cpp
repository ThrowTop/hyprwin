#include "overlay/service.hpp"

#include "log/log.hpp"
#include "overlay/bounds.hpp"
#include "overlay/overlay_window.hpp"
#include "overlay/render/renderer.hpp"
#include "overlay/session.hpp"
#include "util/thread_priority.hpp"
#include "win/native.hpp"

#include <algorithm>

namespace hw {
namespace {
    bool RecoverRenderer(OverlayRenderer& renderer, RenderStatus status, const DebugSettings& debug) noexcept {
        switch (status) {
            case RenderStatus::Ok:
                return true;
            case RenderStatus::SwapchainInvalid:
            case RenderStatus::DeviceLost:
                if (renderer.ResetDevice()) {
                    if (debug.enabled(DebugFlag::Overlay)) {
                        LOG_DEBUG("overlay_service: renderer recovery succeeded status={}", RenderStatusName(status));
                    }
                    return true;
                }
                LOG_ERROR("overlay_service: renderer recovery failed status={}", RenderStatusName(status));
                return false;
        }

        return false;
    }

    void HandleDisplayChange(OverlayWindow& window, OverlayRenderer& renderer) noexcept {
        window.ForceVirtualDesktopReapply();
        renderer.HandleDisplayChange(window.Bounds());
    }
} // namespace

OverlayService::OverlayService(HINSTANCE instance, std::atomic<POINT>* latestMousePos, AtomicSettingsPtr* settings)
    : m_instance(instance)
    , m_latestMousePos(latestMousePos)
    , m_settings(settings)
    , m_outlineManager([this](outline::Update update) { PublishOutlineUpdate(std::move(update)); }) {}

OverlayService::~OverlayService() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_cv.notify_one();
        m_thread.join();
    }
}

void OverlayService::Start() {
    std::lock_guard lock(m_mutex);
    if (m_started) {
        return;
    }

    m_started = true;
    m_thread = std::jthread([this](std::stop_token token) {
        const ::util::ScopedThreadPriorityBoost priorityBoost;
        SET_THREAD_NAME("OVR");
        OverlayLoop(token);
    });
}

bool OverlayService::Send(const OverlayCmd& cmd) noexcept {
    const bool beginsInteraction = std::holds_alternative<BeginDrag>(cmd) || std::holds_alternative<BeginResize>(cmd);
    if (beginsInteraction) {
        bool expected = false;
        if (!m_interactionReserved.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return false;
        }
    }

    if (!m_commands.push(cmd)) {
        if (beginsInteraction) {
            m_interactionReserved.store(false, std::memory_order_release);
        }
        LOG_ERROR("Overlay command queue full; dropping command");
        return false;
    }

    m_cv.notify_one();
    return true;
}

void OverlayService::MarkSettingsDirty() noexcept {
    m_settingsDirty.store(true, std::memory_order_release);
    m_cv.notify_one();
}

void OverlayService::PublishOutlineUpdate(outline::Update update) noexcept {
    {
        std::lock_guard lock(m_mutex);
        m_pendingOutlineCommand = std::visit([](auto&& value) -> OverlayCmd { return std::forward<decltype(value)>(value); }, std::move(update));
        m_outlineCommandPending.store(true, std::memory_order_release);
    }
    m_cv.notify_one();
}

void OverlayService::OverlayLoop(std::stop_token token) noexcept {
    OverlayWindow window;
    OverlayRenderer renderer;

    if (!window.Init(m_instance)) {
        LOG_CRITICAL("Failed to initialize overlay window");
        return;
    }

    if (!renderer.Init(window.hwnd(), window.Bounds())) {
        LOG_CRITICAL("Failed to initialize overlay renderer");
        return;
    }

    SettingsPtr settingsSnapshot = LoadSettingsSnapshot(m_settings);
    m_outlineManager.ApplySettings(*settingsSnapshot);
    if (!RecoverRenderer(renderer, renderer.Render(window.Bounds(), settingsSnapshot, SessionType::None, 1.0f), settingsSnapshot->debug)) {
        LOG_CRITICAL("Failed to prime overlay renderer");
        return;
    }

    OverlayActiveSession active;
    PreviewState preview;

    while (!token.stop_requested()) {
        if (!session::IsActive(active)) {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&] {
                return token.stop_requested() || !m_commands.empty() || !m_placementResults.empty() || m_outlineCommandPending.load(std::memory_order_acquire) ||
                       m_settingsDirty.load(std::memory_order_relaxed);
            });
            lock.unlock();

            if (token.stop_requested()) {
                break;
            }

            window.PumpMessages();
            if (window.ConsumeDisplayChanged()) {
                HandleDisplayChange(window, renderer);
            }

            DrainPlacementResults(renderer, active, preview, settingsSnapshot->debug);
            DrainCommands(renderer, active, settingsSnapshot, preview);
            RetirePlacementWorkerIfUnused(active, *settingsSnapshot);

            if (!session::IsActive(active)) {
                continue;
            }

            window.UpdateVirtualDesktop();
            renderer.UpdateCanvas(window.Bounds());
            const RenderStatus clearStatus = renderer.ClearForShow();
            if (clearStatus != RenderStatus::Ok) {
                LOG_ERROR("overlay_service: ClearForShow failed status={}", RenderStatusName(clearStatus));
                RecoverRenderer(renderer, clearStatus, settingsSnapshot->debug);
                RestoreBeforeTeardown(active, preview);
                CompleteInteraction(renderer, active, preview);
                continue;
            }
            window.Show();
        }

        window.PumpMessages();
        if (window.ConsumeDisplayChanged()) {
            HandleDisplayChange(window, renderer);
        }
        DrainPlacementResults(renderer, active, preview, settingsSnapshot->debug);
        DrainCommands(renderer, active, settingsSnapshot, preview);
        RetirePlacementWorkerIfUnused(active, *settingsSnapshot);

        if (token.stop_requested()) {
            break;
        }

        if (!session::IsActive(active)) {
            window.Hide();
            continue;
        }

        const POINT cursor = m_latestMousePos ? m_latestMousePos->load(std::memory_order_relaxed) : POINT{};
        const vec::i4 logicalBounds = (preview.parkPending || preview.finishing) ? m_latestBounds : session::ComputeBounds(active, cursor);
        m_latestBounds = logicalBounds;

        const vec::i4 visualBounds = ApplyVisualOffset(logicalBounds, session::VisualOffset(active));
        const SessionType sessionType = GetSessionType(active);
        const RenderStatus renderStatus = renderer.Render(visualBounds, settingsSnapshot, sessionType, session::DpiScale(active));
        if (renderStatus != RenderStatus::Ok) {
            LOG_ERROR("overlay_service: Render failed status={}", RenderStatusName(renderStatus));
            RecoverRenderer(renderer, renderStatus, settingsSnapshot->debug);
            RestoreBeforeTeardown(active, preview);
            CompleteInteraction(renderer, active, preview);
        } else if (preview.capturePending) {
            preview.capturePending = false;
            if (settingsSnapshot->debug.enabled(DebugFlag::Snapshot)) {
                LOG_DEBUG("interaction: id={} first outline presented; snapshot capture starting", session::Id(active));
            }
            const thumbnail::WindowSnapshotStatus status = renderer.BeginSnapshotCapture(session::Target(active));
            preview.captureInProgress = status == thumbnail::WindowSnapshotStatus::Pending;
            preview.parkPending = status == thumbnail::WindowSnapshotStatus::Ready;
            if (status == thumbnail::WindowSnapshotStatus::Failed) {
                if (settingsSnapshot->debug.enabled(DebugFlag::Snapshot)) {
                    LOG_DEBUG("interaction: id={} snapshot capture unavailable; continuing overlay-only", session::Id(active));
                }
            }
        } else if (preview.captureInProgress) {
            const thumbnail::WindowSnapshotStatus status = renderer.UpdateSnapshotCapture();
            if (status != thumbnail::WindowSnapshotStatus::Pending) {
                preview.captureInProgress = false;
                preview.parkPending = status == thumbnail::WindowSnapshotStatus::Ready;
                if (settingsSnapshot->debug.enabled(DebugFlag::Snapshot)) {
                    LOG_DEBUG("interaction: id={} snapshot capture finished success={}", session::Id(active), status == thumbnail::WindowSnapshotStatus::Ready);
                }
            }
        } else if (preview.parkPending) {
            const InteractionId interactionId = session::Id(active);
            if (settingsSnapshot->debug.enabled(DebugFlag::Snapshot)) {
                LOG_DEBUG("interaction: id={} first thumbnail frame presented; queuing park", interactionId);
            }
            const vec::i4 originalRawRect = session::OriginalRawRect(active);
            const vec::i4 virtualBounds = win::GetVirtualScreenBounds();
            const vec::i4 parkedRawRect{
              virtualBounds.x2 + 64,
              virtualBounds.y,
              virtualBounds.x2 + 64 + originalRawRect.Width(),
              virtualBounds.y + originalRawRect.Height(),
            };
            if (settingsSnapshot->debug.enabled(DebugFlag::WindowPlacement)) {
                LOG_DEBUG("interaction: id={} park submitting target={:p} virtual_bounds={} requested_raw_rect={}",
                  interactionId,
                  reinterpret_cast<void*>(session::Target(active)),
                  virtualBounds,
                  parkedRawRect);
            }
            EnsurePlacementWorker(settingsSnapshot->debug).Submit(PlacementRequest{
              .interactionId = interactionId,
              .kind = PlacementKind::Park,
              .target = session::Target(active),
              .rawRect = parkedRawRect,
            });
            preview.parkSubmitted = true;
            preview.parkPending = false;
        } else if (preview.mode == OverlayPreview::Live && !preview.finishing) {
            const auto now = std::chrono::steady_clock::now();
            const vec::i4 comparisonRawRect = preview.livePlacementSubmitted ? preview.lastLiveRawRect : session::OriginalRawRect(active);
            const bool rateReady = preview.liveRate == 0 || now >= preview.nextLivePlacement;
            if (rateReady && logicalBounds != comparisonRawRect) {
                EnsurePlacementWorker(settingsSnapshot->debug).Submit(PlacementRequest{
                  .interactionId = session::Id(active),
                  .kind = PlacementKind::Live,
                  .target = session::Target(active),
                  .rawRect = logicalBounds,
                });
                preview.livePlacementSubmitted = true;
                preview.lastLiveRawRect = logicalBounds;
                if (preview.liveRate != 0) {
                    const std::uint64_t intervalNs = std::max<std::uint64_t>(1, 1'000'000'000ULL / preview.liveRate);
                    preview.nextLivePlacement = now + std::chrono::nanoseconds{intervalNs};
                }
            }
        }
    }

    RestoreBeforeTeardown(active, preview);
    CompleteInteraction(renderer, active, preview);
    window.Hide();
    renderer.Destroy();
    window.Destroy();
}
} // namespace hw
