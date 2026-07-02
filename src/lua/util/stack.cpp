#include "lua/util/stack.hpp"

#include <cstdlib>

namespace lua::util {

std::string toString(lua_State* state, int index) {
    size_t len = 0;
    const char* text = lua_tolstring(state, index, &len);
    return text ? std::string(text, len) : std::string{};
}

void setFn(lua_State* state, const char* name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, -2, name);
}

[[noreturn]] void raise(lua_State* state, std::string_view text) {
    lua_pushlstring(state, text.data(), text.size());
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
