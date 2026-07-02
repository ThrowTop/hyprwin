#include "lua/timeout.hpp"

namespace lua {
namespace {
    thread_local TimeoutContext* g_timeoutContext = nullptr;

    void TimeoutHook(lua_State* state, lua_Debug*) {
        TimeoutContext* context = g_timeoutContext;
        if (context && context->active && std::chrono::steady_clock::now() > context->deadline) {
            lua_sethook(state, nullptr, 0, 0);
            context->active = false;
            g_timeoutContext = nullptr;
            luaL_error(state, "Lua execution timed out after %dms", static_cast<int>(kLuaTimeout.count()));
        }
    }
} // namespace

void InstallTimeout(lua_State* state, TimeoutContext& context) noexcept {
    if (!state) {
        return;
    }

    context.deadline = std::chrono::steady_clock::now() + kLuaTimeout;
    context.active = true;
    g_timeoutContext = &context;
    lua_sethook(state, TimeoutHook, LUA_MASKCOUNT, kLuaHookInstructionInterval);
}

void ClearTimeout(lua_State* state) noexcept {
    if (!state) {
        return;
    }

    lua_sethook(state, nullptr, 0, 0);
    if (g_timeoutContext) {
        g_timeoutContext->active = false;
    }
    g_timeoutContext = nullptr;
}
} // namespace lua