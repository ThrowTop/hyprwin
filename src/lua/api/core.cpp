#include "lua/api/internal.hpp"

#include "version.hpp"

#include "log/log.hpp"
#include "lua/binds/key_event.hpp"
#include "lua/binds/key_parse.hpp"
#include "lua/util/stack.hpp"
#include "shader/compiler.hpp"
#include "util/strings.hpp"
#include "win/native.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <luajit.h>
#include <windows.h>

namespace lua::core {
namespace {

    logging::Level parseLogLevel(std::string_view level) noexcept {
        if (level == "trace")
            return logging::Level::Trace;
        if (level == "debug")
            return logging::Level::Debug;
        if (level == "info")
            return logging::Level::Info;
        if (level == "warn")
            return logging::Level::Warn;
        if (level == "error")
            return logging::Level::Error;
        if (level == "critical")
            return logging::Level::Critical;
        return logging::Level::Info;
    }

    void RunAsync(std::wstring path, std::wstring args, std::wstring cwd, bool admin) {
        std::thread([p = std::move(path), a = std::move(args), cwd = std::move(cwd), admin] {
            if (!win::RunProcess(p, a, cwd, admin)) {
                LOG_WARN("hw.run failed: {} cwd={}", ::util::WideToUtf8(p), ::util::WideToUtf8(cwd));
            }
        }).detach();
    }

    void LaunchAsync(std::wstring path, std::wstring args, std::wstring cwd, bool admin) {
        std::thread([p = std::move(path), a = std::move(args), cwd = std::move(cwd), admin] {
            if (!win::LaunchApp(p, a, cwd, admin)) {
                LOG_WARN("hw.launch failed: {} cwd={}", ::util::WideToUtf8(p), ::util::WideToUtf8(cwd));
            }
        }).detach();
    }

    std::wstring DefaultRunCwd(lua_State* state) {
        Context* context = lua::context(state);
        if (!context || context->config_path.empty()) {
            return {};
        }
        return std::filesystem::path{::util::Utf8ToWide(context->config_path)}.parent_path().wstring();
    }

    std::wstring OptionalWideField(lua_State* state, int index, const char* api, const char* name) {
        lua_getfield(state, index, name);
        std::wstring value;
        if (!lua_isnil(state, -1)) {
            if (!lua_isstring(state, -1)) {
                luaL_error(state, "%s: '%s' must be a string", api, name);
            }
            value = ::util::Utf8ToWide(util::toString(state, -1));
        }
        lua_pop(state, 1);
        return value;
    }

    bool OptionalBoolField(lua_State* state, int index, const char* api, const char* name) {
        lua_getfield(state, index, name);
        bool value = false;
        if (!lua_isnil(state, -1)) {
            if (!lua_isboolean(state, -1)) {
                luaL_error(state, "%s: '%s' must be a boolean", api, name);
            }
            value = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
        return value;
    }

    void ParseRunArgs(lua_State* state, const char* api, std::wstring& path, std::wstring& args, std::wstring& cwd, bool& admin) {
        cwd = DefaultRunCwd(state);

        if (lua_istable(state, 1)) {
            path = OptionalWideField(state, 1, api, "path");
            if (path.empty()) {
                path = OptionalWideField(state, 1, api, "file");
            }
            if (path.empty()) {
                path = OptionalWideField(state, 1, api, "target");
            }
            if (path.empty()) {
                luaL_error(state, "%s: 'path', 'file', or 'target' is required", api);
            }

            args = OptionalWideField(state, 1, api, "args");

            std::wstring raw_cwd = OptionalWideField(state, 1, api, "cwd");
            if (raw_cwd.empty()) {
                raw_cwd = OptionalWideField(state, 1, api, "dir");
            }
            if (!raw_cwd.empty()) {
                cwd = std::move(raw_cwd);
            }

            admin = OptionalBoolField(state, 1, api, "admin");
        } else {
            luaL_checktype(state, 1, LUA_TSTRING);
            path = ::util::Utf8ToWide(util::toString(state, 1));
            if (lua_gettop(state) >= 2 && !lua_isnil(state, 2)) {
                luaL_checktype(state, 2, LUA_TSTRING);
                args = ::util::Utf8ToWide(util::toString(state, 2));
            }
            admin = lua_gettop(state) >= 3 && !lua_isnil(state, 3) && lua_toboolean(state, 3) != 0;
        }
    }

    constexpr char kTimerMetatable[] = "HW.Timer";

    struct TimerHandle {
        std::shared_ptr<bool> cancelled;
        int ref = LUA_NOREF;
    };

    int hwTimerGc(lua_State* state) {
        auto* h = static_cast<TimerHandle*>(luaL_checkudata(state, 1, kTimerMetatable));
        if (h->cancelled)
            *h->cancelled = true;
        if (h->ref != LUA_NOREF) {
            luaL_unref(state, LUA_REGISTRYINDEX, h->ref);
            h->ref = LUA_NOREF;
        }
        h->~TimerHandle();
        return 0;
    }

    int hwTimerCancel(lua_State* state) {
        auto* h = static_cast<TimerHandle*>(luaL_checkudata(state, 1, kTimerMetatable));
        if (h->cancelled)
            *h->cancelled = true;
        return 0;
    }

    int hwTimerCall(lua_State* state) {
        auto* h = static_cast<TimerHandle*>(luaL_checkudata(state, 1, kTimerMetatable));
        if (h->ref == LUA_NOREF)
            return 0;
        lua_rawgeti(state, LUA_REGISTRYINDEX, h->ref);
        lua_call(state, 0, 0);
        return 0;
    }

    void EnsureTimerMetatable(lua_State* state) {
        util::ensureMetatable(state, kTimerMetatable, [](lua_State* s) {
            lua_pushvalue(s, -1);
            lua_setfield(s, -2, "__index");
            util::setFn(s, "__gc", hwTimerGc);
            util::setFn(s, "cancel", hwTimerCancel);
            util::setFn(s, "call", hwTimerCall);
        });
    }

    int hwTimer(lua_State* state) {
        const lua_Integer raw = luaL_checkinteger(state, 1);
        if (raw < 0)
            return luaL_argerror(state, 1, "delay must be >= 0");
        if (raw > static_cast<lua_Integer>(std::numeric_limits<int>::max()))
            return luaL_argerror(state, 1, "delay too large");
        const int ms = static_cast<int>(raw);
        luaL_checktype(state, 2, LUA_TFUNCTION);

        bool repeating = false;
        if (lua_gettop(state) >= 3 && lua_istable(state, 3)) {
            lua_getfield(state, 3, "repeating");
            repeating = lua_toboolean(state, -1) != 0;
            lua_pop(state, 1);
        }

        Context* ctx = lua::context(state);
        if (!ctx || !ctx->schedule_timer)
            return luaL_error(state, "timer context missing");

        lua_pushvalue(state, 2);
        const int sched_ref = luaL_ref(state, LUA_REGISTRYINDEX);
        lua_pushvalue(state, 2);
        const int call_ref = luaL_ref(state, LUA_REGISTRYINDEX);
        auto cancelled = ctx->schedule_timer(sched_ref, ms, repeating);

        EnsureTimerMetatable(state);
        auto* h = static_cast<TimerHandle*>(lua_newuserdata(state, sizeof(TimerHandle)));
        new (h) TimerHandle{std::move(cancelled), call_ref};
        luaL_getmetatable(state, kTimerMetatable);
        lua_setmetatable(state, -2);
        return 1;
    }

    int hwBind(lua_State* state) {
        Context* context = lua::context(state);
        if (!context || !context->binds) {
            util::raise(state, "lua api context missing");
        }

        luaL_checktype(state, 1, LUA_TSTRING);
        luaL_checktype(state, 2, LUA_TFUNCTION);

        const std::string key = util::toString(state, 1);
        std::vector<KeyEvent> events;
        std::string error;
        if (!ParseKeyBinding(key, events, error)) {
            return luaL_error(state, "invalid bind '%s': %s", key.c_str(), error.c_str());
        }

        for (const KeyEvent& event : events) {
            if (!context->binds->Add(state, event, 2)) {
                return luaL_error(state, "failed to register bind '%s'", key.c_str());
            }
        }
        return 0;
    }

    std::vector<std::string> BindNames(Context* context) {
        std::vector<std::string> names;
        if (!context || !context->binds) {
            return names;
        }
        context->binds->ForEach([&](const KeyEvent& event) { names.push_back(FormatKeyEvent(event)); });
        std::sort(names.begin(), names.end());
        return names;
    }

    int hwBinds(lua_State* state) {
        const auto names = BindNames(lua::context(state));
        lua_createtable(state, static_cast<int>(names.size()), 0);
        int index = 1;
        for (const auto& name : names) {
            lua_pushlstring(state, name.data(), name.size());
            lua_rawseti(state, -2, index++);
        }
        return 1;
    }

    int hwDebugJit(lua_State* state) {
        luaL_checktype(state, 1, LUA_TBOOLEAN);
        const bool enabled = lua_toboolean(state, 1) != 0;
        luaJIT_setmode(state, 0, LUAJIT_MODE_ENGINE | (enabled ? LUAJIT_MODE_ON : LUAJIT_MODE_OFF));
        LOG_DEBUG("lua JIT {}", enabled ? "enabled" : "disabled");
        return 0;
    }

    int hwDebugShaderCompilerStatus(lua_State* state) {
        const auto status = hw::shader::Compiler::CheckAvailability();
        lua_createtable(state, 0, 2);
        lua_pushboolean(state, status.available);
        lua_setfield(state, -2, "available");
        lua_pushlstring(state, status.diagnostics.data(), status.diagnostics.size());
        lua_setfield(state, -2, "diagnostics");
        return 1;
    }

    int hwDebugIndex(lua_State* state) {
        const std::string key = util::toString(state, 2);
        if (key == "jit") {
            lua_pushcfunction(state, hwDebugJit);
            return 1;
        }
        if (key == "shader_compiler_status") {
            lua_pushcfunction(state, hwDebugShaderCompilerStatus);
            return 1;
        }
        lua_pushnil(state);
        return 1;
    }

    int hwDebugNewIndex(lua_State* state) {
        return luaL_error(state, "unknown hw.debug assignment");
    }

    int hwLog(lua_State* state) {
        Context* context = lua::context(state);
        if (context && context->timeout && util::timedOut(*context->timeout)) {
            lua_sethook(state, nullptr, 0, 0);
            context->timeout->active = false;
            return luaL_error(state, "Lua execution timed out after %dms", static_cast<int>(kLuaTimeout.count()));
        }

        luaL_checktype(state, 1, LUA_TSTRING);
        luaL_checktype(state, 2, LUA_TSTRING);

        const std::string level = util::toString(state, 1);
        const std::string message = util::toString(state, 2);
        logging::write(parseLogLevel(level), "lua: {}", message);
        return 0;
    }

    int hwLogPath(lua_State* state) {
        const std::string path = logging::file_path();
        lua_pushlstring(state, path.data(), path.size());
        return 1;
    }

    int hwOpenLog(lua_State* state) {
        const std::string path = logging::file_path();
        if (path.empty()) {
            lua_pushboolean(state, 0);
            return 1;
        }

        RunAsync(::util::Utf8ToWide(path), {}, {}, false);
        lua_pushboolean(state, 1);
        return 1;
    }

    int hwMsgbox(lua_State* state) {
        const std::string message = util::toString(state, 1);
        const std::string title = lua_gettop(state) >= 2 ? util::toString(state, 2) : std::string{"HyprWin"};
        win::ShowMessageBoxAsync(::util::Utf8ToWide(message), ::util::Utf8ToWide(title), MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    int hwRun(lua_State* state) {
        std::wstring path;
        std::wstring args;
        std::wstring cwd;
        bool admin = false;

        ParseRunArgs(state, "hw.run", path, args, cwd, admin);

        RunAsync(std::move(path), std::move(args), std::move(cwd), admin);
        return 0;
    }

    int hwLaunch(lua_State* state) {
        std::wstring path;
        std::wstring args;
        std::wstring cwd;
        bool admin = false;

        ParseRunArgs(state, "hw.launch", path, args, cwd, admin);

        LaunchAsync(std::move(path), std::move(args), std::move(cwd), admin);
        return 0;
    }

    int hwReload(lua_State* state) {
        Context* context = lua::context(state);
        if (!context || !context->loading_config || !context->request_config_reload) {
            lua_pushboolean(state, 0);
            return 1;
        }
        if (*context->loading_config || (context->lifecycle_action && *context->lifecycle_action)) {
            LOG_WARN("lua: ignored hw.reload() during config load or lifecycle callback");
            lua_pushboolean(state, 0);
            return 1;
        }

        lua_pushboolean(state, context->request_config_reload());
        return 1;
    }

    int hwQuit(lua_State* state) {
        Context* context = lua::context(state);
        if (!context || !context->loading_config || !context->request_app_exit) {
            lua_pushboolean(state, 0);
            return 1;
        }
        if (*context->loading_config || (context->lifecycle_action && *context->lifecycle_action)) {
            LOG_WARN("lua: ignored hw.quit() during config load or lifecycle callback");
            lua_pushboolean(state, 0);
            return 1;
        }

        lua_pushboolean(state, context->request_app_exit());
        return 1;
    }

    void SetCallback(lua_State* state, int* ref) {
        luaL_checktype(state, 1, LUA_TFUNCTION);
        if (!ref) {
            util::raise(state, "lua api context missing");
        }
        if (*ref != LUA_NOREF) {
            luaL_unref(state, LUA_REGISTRYINDEX, *ref);
        }
        lua_pushvalue(state, 1);
        *ref = luaL_ref(state, LUA_REGISTRYINDEX);
    }

    int hwOnReload(lua_State* state) {
        Context* context = lua::context(state);
        SetCallback(state, context ? context->reload_ref : nullptr);
        return 0;
    }

    int hwOnExit(lua_State* state) {
        Context* context = lua::context(state);
        SetCallback(state, context ? context->exit_ref : nullptr);
        return 0;
    }

    int hwConfigPath(lua_State* state) {
        Context* context = lua::context(state);
        if (!context) {
            lua_pushnil(state);
            return 1;
        }
        lua_pushlstring(state, context->config_path.data(), context->config_path.size());
        return 1;
    }

    int hwOpenConfig(lua_State* state) {
        Context* context = lua::context(state);
        if (!context || context->config_path.empty()) {
            lua_pushboolean(state, 0);
            return 1;
        }
        RunAsync(::util::Utf8ToWide(context->config_path), {}, {}, false);
        lua_pushboolean(state, 1);
        return 1;
    }

    int hwNotify(lua_State* state) {
        luaL_checktype(state, 1, LUA_TTABLE);
        Context* context = lua::context(state);
        if (!context || !context->request_notification) {
            lua_pushboolean(state, 0);
            return 1;
        }

        Notification notification{
          .title = L"HyprWin",
          .level = "info",
        };

        lua_getfield(state, 1, "body");
        if (!lua_isstring(state, -1)) {
            return luaL_error(state, "hw.notify: 'body' must be a string");
        }
        notification.body = ::util::Utf8ToWide(util::toString(state, -1));
        lua_pop(state, 1);

        lua_getfield(state, 1, "title");
        if (!lua_isnil(state, -1)) {
            if (!lua_isstring(state, -1))
                return luaL_error(state, "hw.notify: 'title' must be a string");
            notification.title = ::util::Utf8ToWide(util::toString(state, -1));
        }
        lua_pop(state, 1);

        lua_getfield(state, 1, "level");
        if (!lua_isnil(state, -1)) {
            if (!lua_isstring(state, -1))
                return luaL_error(state, "hw.notify: 'level' must be a string");
            notification.level = util::toString(state, -1);
            if (notification.level != "info" && notification.level != "warn" && notification.level != "error") {
                return luaL_error(state, "hw.notify: 'level' must be 'info', 'warn', or 'error'");
            }
        }
        lua_pop(state, 1);

        lua_pushboolean(state, context->request_notification(std::move(notification)));
        return 1;
    }

    int hwBuildIndex(lua_State* state) {
        const std::string key = util::toString(state, 2);
        if (key == "VERSION") {
            lua_pushliteral(state, HYPRWIN_VERSION);
        } else if (key == "DEBUG") {
#ifdef NDEBUG
            lua_pushboolean(state, 0);
#else
            lua_pushboolean(state, 1);
#endif
        } else if (key == "ARCH") {
            lua_pushliteral(state, "x64");
        } else if (key == "LUAJIT_VERSION") {
            lua_pushliteral(state, LUAJIT_VERSION);
        } else if (key == "LUAJIT_MODE") {
            lua_pushliteral(state, "shared");
        } else {
            lua_pushnil(state);
        }
        return 1;
    }

    int hwBuildNewIndex(lua_State* state) {
        return luaL_error(state, "hw.build is read-only");
    }

    void RegisterBuild(lua_State* state) {
        lua_newtable(state);
        lua_newtable(state);
        util::setFn(state, "__index", hwBuildIndex);
        util::setFn(state, "__newindex", hwBuildNewIndex);
        lua_pushliteral(state, "locked");
        lua_setfield(state, -2, "__metatable");
        lua_setmetatable(state, -2);
        lua_setfield(state, -2, "build");
    }

    int hwOnSuper(lua_State* state) {
        Context* context = lua::context(state);
        SetCallback(state, context ? context->super_ref : nullptr);
        return 0;
    }

} // namespace

void registerApi(lua_State* state) {
    EnsureTimerMetatable(state);
    util::setFn(state, "bind", hwBind);
    util::setFn(state, "binds", hwBinds);
    util::setFn(state, "timer", hwTimer);
    util::setFn(state, "on_super", hwOnSuper);
    util::setFn(state, "on_reload", hwOnReload);
    util::setFn(state, "on_exit", hwOnExit);
    util::setFn(state, "log", hwLog);
    util::setFn(state, "log_path", hwLogPath);
    util::setFn(state, "open_log", hwOpenLog);
    util::setFn(state, "msgbox", hwMsgbox);
    util::setFn(state, "notify", hwNotify);
    util::setFn(state, "run", hwRun);
    util::setFn(state, "launch", hwLaunch);
    util::setFn(state, "reload", hwReload);
    util::setFn(state, "quit", hwQuit);
    util::setFn(state, "config_path", hwConfigPath);
    util::setFn(state, "open_config", hwOpenConfig);
    RegisterBuild(state);

    lua_newtable(state);
    lua_newtable(state);
    util::setFn(state, "__index", hwDebugIndex);
    util::setFn(state, "__newindex", hwDebugNewIndex);
    lua_setmetatable(state, -2);
    lua_setfield(state, -2, "debug");
}

} // namespace lua::core
