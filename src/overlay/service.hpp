#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#include <windows.h>

#include "config/settings.hpp"
#include "overlay/atomic_rect.hpp"
#include "overlay/cmd.hpp"
#include "shader/manager.hpp"
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
    [[nodiscard]] vec::i4 GetLatestBounds() const noexcept;

  private:
    void OverlayLoop(std::stop_token token) noexcept;
    void DrainCommands(OverlayRenderer& renderer, OverlayActiveSession& active, SettingsPtr& settingsSnapshot, bool& shutdown, bool& resetDevice) noexcept;
    void ApplyCommand(OverlayRenderer& renderer, const OverlayCmd& cmd, OverlayActiveSession& active, SettingsPtr& settingsSnapshot, bool& shutdown, bool& resetDevice) noexcept;
    void PublishShaderCommand(OverlayCmd cmd) noexcept;

    HINSTANCE m_instance = nullptr;
    std::atomic<POINT>* m_latestMousePos = nullptr;
    AtomicSettingsPtr* m_settings = nullptr;

    util::SpscQueue<OverlayCmd, 16> m_commands{};
    std::atomic_bool m_settingsDirty{false};
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::optional<OverlayCmd> m_pendingShaderCommand;
    std::jthread m_thread;
    AtomicRect m_latestBounds{};
    shader::Manager m_shaderManager;
    bool m_started = false;
};

} // namespace hw
