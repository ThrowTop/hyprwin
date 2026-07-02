#pragma once

#include <chrono>

#include <lua.hpp>

namespace lua {

constexpr auto kLuaTimeout = std::chrono::milliseconds(50);
constexpr int kLuaHookInstructionInterval = 100;

struct TimeoutContext {
    std::chrono::steady_clock::time_point deadline{};
    bool active = false;
};

void InstallTimeout(lua_State* state, TimeoutContext& context) noexcept;
void ClearTimeout(lua_State* state) noexcept;

} // namespace lua
