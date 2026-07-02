#pragma once

#include <string>
#include <string_view>

#include <lua.hpp>

#include "lua/timeout.hpp"

namespace lua::util {

inline int absIndex(lua_State* state, int index) noexcept {
    return index > 0 || index <= LUA_REGISTRYINDEX ? index : lua_gettop(state) + index + 1;
}

// Creates the metatable once, populates it via the callback, and locks __metatable.
template <typename Fn>
void ensureMetatable(lua_State* state, const char* name, Fn&& populate) {
    if (luaL_newmetatable(state, name) != 0) {
        populate(state);
        lua_pushstring(state, name);
        lua_setfield(state, -2, "__metatable");
    }
    lua_pop(state, 1);
}

std::string toString(lua_State* state, int index);
void setFn(lua_State* state, const char* name, lua_CFunction function);
[[noreturn]] void raise(lua_State* state, std::string_view text);
bool timedOut(const ::lua::TimeoutContext& context) noexcept;
int tracebackHandler(lua_State* state);
int pushTracebackHandler(lua_State* state);

} // namespace lua::util
