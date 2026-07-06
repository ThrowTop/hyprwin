#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <windows.h>

#include "config/settings.hpp"
#include "overlay/service.hpp"
#include "util/geometry.hpp"
#include "util/spsc_queue.hpp"

namespace hw {

class Mouse {
  public:
    Mouse(std::atomic<POINT>* latestMousePos, AtomicSettingsPtr* settings, OverlayService* overlay) noexcept;
    Mouse(const Mouse&) = delete;
    Mouse& operator=(const Mouse&) = delete;

    ~Mouse();

    void InstallHook() noexcept;
    void UninstallHook() noexcept;

  private:
    static LRESULT CALLBACK HookProc(int code, WPARAM wparam, LPARAM lparam) noexcept;

    void HookThreadMain(std::stop_token token) noexcept;
    void DispatchThreadMain(std::stop_token token) noexcept;
    bool PushButtonEvent(WPARAM event) noexcept;
    void ProcessButtonEvent(WPARAM event) noexcept;
    void BeginOperation(WPARAM event) noexcept;
    void FinishOperation() noexcept;
    void CancelOperation() noexcept;
    [[nodiscard]] ResizeCorner ResolveResizeCorner(const Settings& settings, vec::i2 pt, const vec::i4& rawRect) const noexcept;

    static inline std::atomic<Mouse*> m_instance{nullptr};

    std::atomic<POINT>* m_latestMousePos = nullptr;
    AtomicSettingsPtr* m_settings = nullptr;
    OverlayService* m_overlay = nullptr;

    std::jthread m_hookThread;
    std::jthread m_dispatchThread;

    std::condition_variable m_hookCv;
    std::mutex m_hookMutex;
    std::atomic_bool m_installRequested{false};
    std::atomic_bool m_uninstallRequested{false};
    std::atomic<DWORD> m_hookThreadId{0};
    HHOOK m_hook = nullptr;

    std::condition_variable m_dispatchCv;
    std::mutex m_dispatchMutex;
    util::SpscQueue<WPARAM, 16> m_buttonQueue{};
    std::atomic_bool m_queueOverflowed{false};
    std::atomic_bool m_finishRequested{false};

    std::atomic<POINT> m_lastDownPt{};
    std::atomic_bool m_allowLeftUpPassthrough{false};
    std::atomic_bool m_allowRightUpPassthrough{false};

    HWND m_target = nullptr;
    SessionType m_sessionType = SessionType::None;
    InteractionId m_interactionId = 0;
    InteractionId m_nextInteractionId = 1;
};

} // namespace hw
