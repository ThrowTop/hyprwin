#pragma once

#include <functional>
#include <memory>
#include <string>

#include <lua.hpp>

#include "config/settings.hpp"
#include "lua/binds/bind_registry.hpp"
#include "lua/runtime.hpp"
#include "lua/timeout.hpp"

namespace lua {

struct Context {
    BindRegistry* binds = nullptr;
    hw::Settings* pending_settings = nullptr;
    bool* loading_config = nullptr;
    bool* lifecycle_action = nullptr;
    int* super_ref = nullptr;
    int* reload_ref = nullptr;
    int* exit_ref = nullptr;
    TimeoutContext* timeout = nullptr;
    std::string config_path;
    std::function<void()> publish_settings;
    std::function<void()> notify_settings_changed;
    std::function<bool()> request_config_reload;
    std::function<bool()> request_app_exit;
    std::function<bool(Notification)> request_notification;
    std::function<std::shared_ptr<bool>(int, int, bool)> schedule_timer;
};

inline char kContextRegistryKey;

inline void storeContext(lua_State* state, Context& context) {
    lua_pushlightuserdata(state, &kContextRegistryKey);
    lua_pushlightuserdata(state, &context);
    lua_rawset(state, LUA_REGISTRYINDEX);
}

inline Context* context(lua_State* state) noexcept {
    lua_pushlightuserdata(state, &kContextRegistryKey);
    lua_rawget(state, LUA_REGISTRYINDEX);
    auto* context = static_cast<Context*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return context;
}

} // namespace lua
