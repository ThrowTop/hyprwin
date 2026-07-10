#include "lua/runtime.hpp"

#include "config/default_config.hpp"
#include "log/log.hpp"
#include "lua/api/internal.hpp"
#include "lua/binds/key_parse.hpp"
#include "lua/context.hpp"
#include "lua/util/stack.hpp"
#include "lua_stdlib_bc.h"
#include "perf/perf.hpp"
#include "util/strings.hpp"
#include "win/native.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

namespace lua {
namespace {

    void PrependConfigPackagePath(lua_State* state, const std::wstring& config_path) {
        const auto config_dir = std::filesystem::path{config_path}.parent_path();
        if (config_dir.empty()) {
            return;
        }

        const std::string prefix = ::util::WideToUtf8(config_dir.wstring()) + "\\?.lua;";

        lua_getglobal(state, "package");
        lua_getfield(state, -1, "path");
        lua_pushfstring(state, "%s%s", prefix.c_str(), lua_tostring(state, -1));
        lua_setfield(state, -3, "path");
        lua_pop(state, 2);
    }

} // namespace

Runtime::Runtime(Services services) : m_services(std::move(services)) {}

Runtime::~Runtime() {
    CloseState();
}

bool Runtime::LoadConfig(std::wstring_view path) {
    return LoadConfigImpl(path);
}

bool Runtime::ReloadConfig(std::wstring_view path) {
    RunLifecycleCallback(m_reloadRef, "reload callback");
    return LoadConfigImpl(path);
}

void Runtime::PrepareExit() noexcept {
    RunLifecycleCallback(m_exitRef, "exit callback");
}

bool Runtime::DispatchBind(const KeyEvent& event) {
    if (!m_state || m_safeMode.active) {
        return false;
    }

    const int ref = m_binds.Find(event);
    if (ref == LUA_NOREF) {
        return false;
    }

    if (m_pending.debug.enabled(hw::DebugFlag::TraceBinds)) {
        logging::write(logging::Level::Trace, "lua bind dispatch: {} (vk={} modifiers={})", FormatKeyEvent(event), event.vk, static_cast<unsigned>(event.modifiers));
    }

    lua_rawgeti(m_state, LUA_REGISTRYINDEX, ref);
    HW_PERF_SCOPE(::perf::CounterId::BindDispatch);
    std::string error;

    std::chrono::steady_clock::time_point bind_start;
    if (m_pending.debug.enabled(hw::DebugFlag::BenchBinds)) {
        bind_start = std::chrono::steady_clock::now();
    }

    const bool ok = GuardedCall(0, 0, 0, "bind callback", &error);

    if (m_pending.debug.enabled(hw::DebugFlag::BenchBinds)) {
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - bind_start).count();
        logging::write(logging::Level::Debug, "lua bind {} took {}us", FormatKeyEvent(event), us);
    }

    return ok;
}

bool Runtime::DispatchSuper(bool pressed) {
    if (!m_state || m_safeMode.active || m_superRef == LUA_NOREF) {
        return false;
    }

    if (m_pending.debug.enabled(hw::DebugFlag::TraceSuper)) {
        logging::write(logging::Level::Trace, "lua super dispatch: {}", pressed ? "pressed" : "released");
    }

    lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_superRef);
    lua_pushboolean(m_state, pressed ? 1 : 0);
    std::string error;
    return GuardedCall(1, 0, 0, "super callback", &error);
}

bool Runtime::LoadConfigImpl(std::wstring_view path) {
    CloseState();
    m_pending = hw::Settings{};
    m_loadingConfig = true;
    const auto load_start = std::chrono::steady_clock::now();

    if (!CreateState()) {
        m_loadingConfig = false;
        EnterSafeMode("failed to create Lua state");
        return false;
    }

    const std::string file = ::util::WideToUtf8(path);
    int loadResult = luaL_loadfile(m_state, file.c_str());
    if (loadResult == LUA_ERRFILE) {
        lua_pop(m_state, 1);
        if (std::ofstream f{std::filesystem::path(path), std::ios::binary}; f) {
            f.write(hw::kDefaultConfig, sizeof(hw::kDefaultConfig) - 1);
            LOG_INFO("lua: no config at {}, created default", file);
        }
        loadResult = luaL_loadbuffer(m_state, hw::kDefaultConfig, sizeof(hw::kDefaultConfig) - 1, "default config");
    }
    if (loadResult != LUA_OK) {
        const std::string error = util::toString(m_state, -1);
        m_loadingConfig = false;
        EnterSafeMode("config load failed: " + error);
        return false;
    }

    std::string error;
    const bool ok = GuardedCall(0, 0, 0, "config execution", &error, false);
    m_loadingConfig = false;
    if (!ok) {
        EnterSafeMode("config execution failed: " + error);
        return false;
    }

    m_safeMode.active = false;
    m_safeMode.reason.clear();
    const auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - load_start).count();
    PublishSettings();
    NotifySettingsChanged();
    LOG_INFO("lua config loaded: {} in {}ms", file, load_ms);
    return true;
}

bool Runtime::CreateState() {
    m_state = luaL_newstate();
    if (!m_state) {
        LOG_CRITICAL("lua: failed to create Lua state");
        return false;
    }

    luaL_openlibs(m_state);
    PrependConfigPackagePath(m_state, m_services.config_path);

    auto context = std::make_unique<Context>();
    context->binds = &m_binds;
    context->pending_settings = &m_pending;
    context->loading_config = &m_loadingConfig;
    context->lifecycle_action = &m_lifecycleAction;
    context->timeout = &m_timeout;
    context->super_ref = &m_superRef;
    context->reload_ref = &m_reloadRef;
    context->exit_ref = &m_exitRef;
    context->config_path = ::util::WideToUtf8(m_services.config_path);
    context->publish_settings = [this] { PublishSettings(); };
    context->notify_settings_changed = [this] { NotifySettingsChanged(); };
    context->request_config_reload = m_services.request_config_reload;
    context->request_app_exit = m_services.request_app_exit;
    context->request_notification = m_services.request_notification;
    context->schedule_timer = [this](int ref, int ms, bool rep) { return ScheduleTimer(ref, ms, rep); };

    api::registerAll(m_state, *context);

    if (luaL_loadbuffer(m_state, reinterpret_cast<const char*>(luaJIT_BC_stdlib), luaJIT_BC_stdlib_SIZE, "stdlib") != LUA_OK || lua_pcall(m_state, 0, 0, 0) != LUA_OK) {
        LOG_CRITICAL("lua stdlib failed: {}", util::toString(m_state, -1));
        lua_close(m_state);
        m_state = nullptr;
        return false;
    }

    vec::cacheConstructors(m_state);
    color::cacheColorCtype(m_state);

    m_context = std::move(context);
    return true;
}

void Runtime::CloseState() noexcept {
    if (!m_state) {
        return;
    }

    m_binds.Clear(m_state);
    if (m_superRef != LUA_NOREF) {
        luaL_unref(m_state, LUA_REGISTRYINDEX, m_superRef);
        m_superRef = LUA_NOREF;
    }
    if (m_reloadRef != LUA_NOREF) {
        luaL_unref(m_state, LUA_REGISTRYINDEX, m_reloadRef);
        m_reloadRef = LUA_NOREF;
    }
    if (m_exitRef != LUA_NOREF) {
        luaL_unref(m_state, LUA_REGISTRYINDEX, m_exitRef);
        m_exitRef = LUA_NOREF;
    }
    while (!m_timerHeap.empty()) {
        luaL_unref(m_state, LUA_REGISTRYINDEX, m_timerHeap.top().lua_ref);
        m_timerHeap.pop();
    }
    m_context.reset();
    lua_close(m_state);
    m_state = nullptr;
}

bool Runtime::GuardedCall(int nargs, int nresults, int errfunc, std::string_view context, std::string* error_out, bool timeout_enabled) {
    if (!m_state) {
        return false;
    }

    int errorHandler = errfunc;
    if (errorHandler == 0) {
        util::pushTracebackHandler(m_state);
        const int functionIndex = lua_gettop(m_state) - nargs - 1;
        lua_insert(m_state, functionIndex);
        errorHandler = functionIndex;
    }

    const bool trace_this_call = m_pending.debug.enabled(hw::DebugFlag::TraceTimeout);
    std::chrono::steady_clock::time_point call_start{};
    if (trace_this_call) {
        call_start = std::chrono::steady_clock::now();
    }

    if (timeout_enabled) {
        InstallTimeout(m_state, m_timeout);
    }
    const int result = lua_pcall(m_state, nargs, nresults, errorHandler);
    if (timeout_enabled) {
        ClearTimeout(m_state);
    }

    if (timeout_enabled && result == LUA_OK && trace_this_call) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - call_start).count();
        if (ms >= kLuaTimeout.count() / 2) {
            logging::write(logging::Level::Warn, "lua {} ran for {}ms ({}ms timeout)", context, ms, kLuaTimeout.count());
        }
    }
    if (result != LUA_OK) {
        const std::string error = util::toString(m_state, -1);
        lua_pop(m_state, 1);
        if (errorHandler != 0) {
            lua_remove(m_state, errorHandler);
        }
        if (error_out) {
            *error_out = error;
        }
        LOG_ERROR("lua {} failed:\n{}", context, error);
        return false;
    }

    if (errorHandler != 0) {
        lua_remove(m_state, errorHandler);
    }
    return true;
}

std::optional<TimerEntry::Clock::time_point> Runtime::NextTimerDeadline() noexcept {
    while (!m_timerHeap.empty() && *m_timerHeap.top().cancelled) {
        luaL_unref(m_state, LUA_REGISTRYINDEX, m_timerHeap.top().lua_ref);
        m_timerHeap.pop();
    }
    if (m_timerHeap.empty())
        return std::nullopt;
    return m_timerHeap.top().deadline;
}

void Runtime::FireExpiredTimers() noexcept {
    if (!m_state || m_timerHeap.empty())
        return;
    const auto now = TimerEntry::Clock::now();
    while (!m_timerHeap.empty()) {
        if (*m_timerHeap.top().cancelled) {
            luaL_unref(m_state, LUA_REGISTRYINDEX, m_timerHeap.top().lua_ref);
            m_timerHeap.pop();
            continue;
        }
        if (m_timerHeap.top().deadline > now)
            break;

        TimerEntry entry = m_timerHeap.top();
        m_timerHeap.pop();

        if (entry.repeat) {
            entry.deadline = now + entry.interval;
            m_timerHeap.push(entry); // re-arm before call; callback may cancel
            lua_rawgeti(m_state, LUA_REGISTRYINDEX, entry.lua_ref);
        } else {
            lua_rawgeti(m_state, LUA_REGISTRYINDEX, entry.lua_ref);
            luaL_unref(m_state, LUA_REGISTRYINDEX, entry.lua_ref);
        }
        GuardedCall(0, 0, 0, "timer callback");
    }
}

std::shared_ptr<bool> Runtime::ScheduleTimer(int lua_ref, int ms, bool repeat) noexcept {
    auto cancelled = std::make_shared<bool>(false);
    const auto interval = std::chrono::milliseconds(ms);
    m_timerHeap.push(TimerEntry{
      .deadline = TimerEntry::Clock::now() + interval,
      .interval = interval,
      .lua_ref = lua_ref,
      .cancelled = cancelled,
      .repeat = repeat,
    });
    return cancelled;
}

void Runtime::EnterSafeMode(std::string reason) noexcept {
    LOG_ERROR("lua safe mode: {}", reason);
    m_safeMode.active = true;
    m_safeMode.reason = std::move(reason);
    win::ShowMessageBoxAsync(::util::Utf8ToWide("Config error:\n\n" + m_safeMode.reason), L"HyprWin", MB_OK | MB_ICONERROR | MB_TOPMOST);
    CloseState();
    m_pending = hw::Settings{};
    PublishSettings();
    NotifySettingsChanged();
}

void Runtime::PublishSettings() noexcept {
    if (m_services.settings) {
        m_services.settings->store(std::make_shared<hw::Settings>(m_pending), std::memory_order_release);
    }
    if (m_services.set_super_key) {
        m_services.set_super_key(m_pending.super_vk);
    }
}

void Runtime::NotifySettingsChanged() noexcept {
    if (m_services.reload_overlay_settings) {
        m_services.reload_overlay_settings();
    }
}

void Runtime::RunLifecycleCallback(int ref, std::string_view name) noexcept {
    if (!m_state || m_safeMode.active || ref == LUA_NOREF || m_lifecycleAction) {
        return;
    }

    m_lifecycleAction = true;
    lua_rawgeti(m_state, LUA_REGISTRYINDEX, ref);
    GuardedCall(0, 0, 0, name);
    m_lifecycleAction = false;
}

} // namespace lua
