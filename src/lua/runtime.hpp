#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

#include <lua.hpp>

#include "config/settings.hpp"
#include "lua/binds/bind_registry.hpp"
#include "lua/binds/key_event.hpp"
#include "lua/safe_mode.hpp"
#include "lua/timeout.hpp"

namespace lua {

struct Context;

struct Notification {
    std::wstring title;
    std::wstring body;
    std::string level;
};

struct TimerEntry {
    using Clock = std::chrono::steady_clock;
    Clock::time_point deadline;
    std::chrono::milliseconds interval{0};
    int lua_ref = LUA_NOREF;
    std::shared_ptr<bool> cancelled;
    bool repeat = false;

    bool operator>(const TimerEntry& o) const noexcept {
        return deadline > o.deadline;
    }
};

class Runtime {
  public:
    struct Services {
        hw::AtomicSettingsPtr* settings = nullptr;
        std::function<void(UINT)> set_super_key;
        std::function<void()> reload_overlay_settings;
        std::function<bool()> request_config_reload;
        std::function<bool()> request_app_exit;
        std::function<bool(Notification)> request_notification;
        std::wstring config_path;
    };

    explicit Runtime(Services services);
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    bool LoadConfig(std::wstring_view path);
    bool ReloadConfig(std::wstring_view path);
    void PrepareExit() noexcept;
    bool DispatchBind(const KeyEvent& event);
    bool DispatchSuper(bool pressed);

    std::optional<TimerEntry::Clock::time_point> NextTimerDeadline() noexcept;
    void FireExpiredTimers() noexcept;
    std::shared_ptr<bool> ScheduleTimer(int lua_ref, int ms, bool repeat) noexcept;

  private:
    bool LoadConfigImpl(std::wstring_view path);
    bool CreateState();
    void CloseState() noexcept;
    bool GuardedCall(int nargs, int nresults, int errfunc, std::string_view context, std::string* error_out = nullptr, bool timeout_enabled = true);
    void EnterSafeMode(std::string reason) noexcept;
    void PublishSettings() noexcept;
    void NotifySettingsChanged() noexcept;
    void RunLifecycleCallback(int ref, std::string_view name) noexcept;

    Services m_services;
    lua_State* m_state = nullptr;
    std::unique_ptr<Context> m_context;
    BindRegistry m_binds;
    int m_superRef = LUA_NOREF;
    int m_reloadRef = LUA_NOREF;
    int m_exitRef = LUA_NOREF;
    hw::Settings m_pending{};
    SafeMode m_safeMode{};
    TimeoutContext m_timeout{};
    bool m_loadingConfig = false;
    bool m_lifecycleAction = false;

    using TimerHeap = std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<TimerEntry>>;
    TimerHeap m_timerHeap;
};

} // namespace lua
