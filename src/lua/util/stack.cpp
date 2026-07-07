#include "lua/util/stack.hpp"

#include <cstdlib>

#include "util/strings.hpp"

namespace lua::util {

std::string toString(lua_State* state, int index) {
    size_t len = 0;
    const char* text = lua_tolstring(state, index, &len);
    return text ? std::string(text, len) : std::string{};
}

void pushString(lua_State* state, std::string_view value) {
    lua_pushlstring(state, value.data(), value.size());
}

void pushWideString(lua_State* state, std::wstring_view value) {
    const std::string utf8 = ::util::WideToUtf8(value);
    pushString(state, utf8);
}

void setFn(lua_State* state, const char* name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, -2, name);
}

void setSelfIndex(lua_State* state) {
    lua_pushvalue(state, -1);
    lua_setfield(state, -2, "__index");
}

void setStringField(lua_State* state, const char* name, std::string_view value) {
    pushString(state, value);
    lua_setfield(state, -2, name);
}

void setWideStringField(lua_State* state, const char* name, std::wstring_view value) {
    pushWideString(state, value);
    lua_setfield(state, -2, name);
}

void setBoolField(lua_State* state, const char* name, bool value) {
    lua_pushboolean(state, value ? 1 : 0);
    lua_setfield(state, -2, name);
}

void setIntegerField(lua_State* state, const char* name, lua_Integer value) {
    lua_pushinteger(state, value);
    lua_setfield(state, -2, name);
}

void setNumberField(lua_State* state, const char* name, lua_Number value) {
    lua_pushnumber(state, value);
    lua_setfield(state, -2, name);
}

[[noreturn]] void raise(lua_State* state, std::string_view text) {
    pushString(state, text);
    lua_error(state);
    std::abort();
}

bool timedOut(const ::lua::TimeoutContext& context) noexcept {
    return context.active && std::chrono::steady_clock::now() > context.deadline;
}

int tracebackHandler(lua_State* state) {
    const char* message = lua_tostring(state, 1);
    if (message) {
        luaL_traceback(state, state, message, 1);
        return 1;
    }

    if (!lua_isnoneornil(state, 1)) {
        if (luaL_callmeta(state, 1, "__tostring")) {
            luaL_traceback(state, state, lua_tostring(state, -1), 1);
            return 1;
        }
    }

    luaL_traceback(state, state, "(non-string Lua error)", 1);
    return 1;
}

int pushTracebackHandler(lua_State* state) {
    lua_pushcfunction(state, tracebackHandler);
    return lua_gettop(state);
}

} // namespace lua::util
