#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

#include <windows.h>

#include "keyboard/key_state.hpp"
#include "lua/runtime.hpp"
#include "util/spsc_queue.hpp"

namespace hw {
class Keyboard {
  public:
    using LuaServices = lua::Runtime::Services;

    struct SuperCallbacks {
        std::function<void()> pressed;
        std::function<void()> released;
    };

    explicit Keyboard(UINT super_vk = VK_LWIN, LuaServices lua_services = {}, SuperCallbacks super_callbacks = {});
    ~Keyboard();

    Keyboard(const Keyboard&) = delete;
    Keyboard& operator=(const Keyboard&) = delete;

    void SetSuperKey(UINT super_vk) noexcept;
    bool RequestReloadConfig() noexcept;
    bool RequestExit() noexcept;

  private:
    static LRESULT CALLBACK HookProc(int code, WPARAM wparam, LPARAM lparam) noexcept;

    struct HookState {
        HHOOK hook = nullptr;
        std::atomic<DWORD> thread_id{0};
        std::atomic_bool super_down{false};
    };

    struct DispatchState {
        KeyState keys;
        std::atomic_bool reload_requested{false};
        std::atomic_bool exit_requested{false};
        bool reload_deferred = false;
        std::atomic_bool exit_handled{false};
    };

    void HookThreadMain(std::stop_token stop_token);
    void DispatchThreadMain(std::stop_token stop_token);
    void ProcessKey(std::uint32_t encoded, lua::Runtime& lua_runtime);
    void SeedModifierStates() noexcept;
    void ProcessReloadRequest(lua::Runtime& lua_runtime);
    void ProcessExitRequest(lua::Runtime& lua_runtime);
    void NotifySuperPressed() noexcept;
    void NotifySuperReleased() noexcept;
    [[nodiscard]] std::uint8_t CurrentModifierMask() const noexcept;
    bool PushKeyFromHook(std::uint32_t encoded) noexcept;

    static inline std::atomic<Keyboard*> m_instance{nullptr};

    std::atomic<UINT> m_superVk{VK_LWIN};
    std::atomic_bool m_queueOverflowed{false};
    LuaServices m_luaServices{};
    std::function<bool()> m_finalizeAppExit;
    std::wstring m_configPath = L"hyprwin.lua";

    SuperCallbacks m_superCallbacks;

    HookState m_hook{};
    DispatchState m_dispatch{};

    std::jthread m_hookThread;
    std::jthread m_dispatchThread;

    std::condition_variable m_dispatchCv;
    std::mutex m_dispatchCvMutex;

    util::SpscQueue<std::uint32_t, 64> m_keyQueue;
};
} // namespace hw
