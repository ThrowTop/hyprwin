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

template <typename Fn>
void setTableField(lua_State* state, const char* name, Fn&& populate) {
    lua_newtable(state);
    populate(state);
    lua_setfield(state, -2, name);
}

std::string toString(lua_State* state, int index);
void pushString(lua_State* state, std::string_view value);
void pushWideString(lua_State* state, std::wstring_view value);
void setFn(lua_State* state, const char* name, lua_CFunction function);
void setSelfIndex(lua_State* state);
void setStringField(lua_State* state, const char* name, std::string_view value);
void setWideStringField(lua_State* state, const char* name, std::wstring_view value);
void setBoolField(lua_State* state, const char* name, bool value);
void setIntegerField(lua_State* state, const char* name, lua_Integer value);
void setNumberField(lua_State* state, const char* name, lua_Number value);
[[noreturn]] void raise(lua_State* state, std::string_view text);
bool timedOut(const ::lua::TimeoutContext& context) noexcept;
int tracebackHandler(lua_State* state);
int pushTracebackHandler(lua_State* state);

} // namespace lua::util
