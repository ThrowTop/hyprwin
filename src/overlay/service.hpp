#pragma once

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <windows.h>

#include "config/settings.hpp"
#include "overlay/cmd.hpp"
#include "overlay/outline/manager.hpp"
#include "overlay/placement/worker.hpp"
#include "util/spsc_queue.hpp"

namespace hw {

class OverlayRenderer;

class OverlayService {
  public:
    OverlayService(HINSTANCE instance, std::atomic<POINT>* latestMousePos, AtomicSettingsPtr* settings);
    OverlayService(const OverlayService&) = delete;
    OverlayService& operator=(const OverlayService&) = delete;

    ~OverlayService();

    void Start();
    bool Send(const OverlayCmd& cmd) noexcept;
    void MarkSettingsDirty() noexcept;

  private:
    struct PreviewState {
        OverlayPreview mode = OverlayPreview::Overlay;
        std::uint32_t liveRate = 60;
        bool capturePending = false;
        bool captureInProgress = false;
        bool parkPending = false;
        bool parkSubmitted = false;
        bool finishing = false;
        bool livePlacementSubmitted = false;
        vec::i4 lastLiveRawRect{};
        std::chrono::steady_clock::time_point nextLivePlacement{};

        void Reset() noexcept {
            mode = OverlayPreview::Overlay;
            liveRate = 60;
            capturePending = false;
            captureInProgress = false;
            parkPending = false;
            parkSubmitted = false;
            finishing = false;
            livePlacementSubmitted = false;
            lastLiveRawRect = {};
            nextLivePlacement = {};
        }
    };

    void OverlayLoop(std::stop_token token) noexcept;
    void DrainCommands(
      OverlayRenderer& renderer,
      OverlayActiveSession& active,
      SettingsPtr& settingsSnapshot,
      PreviewState& preview) noexcept;
    void ApplyCommand(OverlayRenderer& renderer,
      const OverlayCmd& cmd,
      OverlayActiveSession& active,
      SettingsPtr& settingsSnapshot,
      PreviewState& preview) noexcept;
    void DrainPlacementResults(OverlayRenderer& renderer,
      OverlayActiveSession& active,
      PreviewState& preview,
      const DebugSettings& debug) noexcept;
    void RestoreBeforeTeardown(const OverlayActiveSession& active, PreviewState& preview) noexcept;
    void CompleteInteraction(OverlayRenderer& renderer, OverlayActiveSession& active, PreviewState& preview) noexcept;
    PlacementWorker& EnsurePlacementWorker(const DebugSettings& debug);
    void RetirePlacementWorkerIfUnused(const OverlayActiveSession& active, const Settings& settings) noexcept;
    void PublishOutlineUpdate(outline::Update update) noexcept;
    void PublishPlacementResult(const PlacementResult& result) noexcept;

    HINSTANCE m_instance = nullptr;
    std::atomic<POINT>* m_latestMousePos = nullptr;
    AtomicSettingsPtr* m_settings = nullptr;

    util::SpscQueue<OverlayCmd, 16> m_commands{};
    util::SpscQueue<PlacementResult, 16> m_placementResults{};
    std::atomic_bool m_settingsDirty{false};
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::optional<OverlayCmd> m_pendingOutlineCommand;
    std::atomic_bool m_outlineCommandPending{false};
    std::jthread m_thread;
    std::atomic_bool m_interactionReserved{false};
    vec::i4 m_latestBounds{};
    outline::Manager m_outlineManager;
    std::unique_ptr<PlacementWorker> m_placementWorker;
    bool m_started = false;
};

} // namespace hw
