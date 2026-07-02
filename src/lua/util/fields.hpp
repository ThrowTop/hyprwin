#pragma once

#include <cmath>
#include <optional>
#include <string_view>
#include <tuple>

#include <lua.hpp>

namespace lua::fields {

// Default push/apply for types used with auto_field.
// Specialise in your TU for additional types.
template <typename T>
void luaPush(lua_State* state, const T& val) = delete;
template <typename T>
std::optional<T> luaApply(lua_State* state, int idx) = delete;

template <>
inline void luaPush<bool>(lua_State* state, const bool& v) {
    lua_pushboolean(state, v);
}
template <>
inline std::optional<bool> luaApply<bool>(lua_State* state, int idx) {
    if (!lua_isboolean(state, idx))
        return std::nullopt;
    return lua_toboolean(state, idx) != 0;
}

template <>
inline void luaPush<float>(lua_State* state, const float& v) {
    lua_pushnumber(state, v);
}
template <>
inline std::optional<float> luaApply<float>(lua_State* state, int idx) {
    if (!lua_isnumber(state, idx))
        return std::nullopt;
    const float v = static_cast<float>(lua_tonumber(state, idx));
    if (!std::isfinite(v))
        return std::nullopt;
    return v;
}

template <typename S, typename T, typename PushFn, typename ApplyFn>
struct Field {
    std::string_view name;
    T S::* ptr;
    PushFn push;   // (lua_State*, const T&) -> void
    ApplyFn apply; // (lua_State*, int)      -> std::optional<T>
};

template <typename S, typename T, typename PushFn, typename ApplyFn>
Field(std::string_view, T S::*, PushFn, ApplyFn) -> Field<S, T, PushFn, ApplyFn>;

// Uses luaPush<T>/luaApply<T> -- specialise those for the type.
template <typename S, typename T>
auto auto_field(std::string_view name, T S::* ptr) {
    return Field{name, ptr, [](lua_State* s, const T& v) { luaPush<T>(s, v); }, [](lua_State* s, int idx) { return luaApply<T>(s, idx); }};
}

// Explicit push/apply lambdas -- no specialisation needed for the type.
template <typename S, typename T, typename PushFn, typename ApplyFn>
auto custom_field(std::string_view name, T S::* ptr, PushFn push, ApplyFn apply) {
    return Field{name, ptr, std::move(push), std::move(apply)};
}

template <typename S, typename... Fs>
bool pushField(lua_State* state, const S& s, std::string_view key, const std::tuple<Fs...>& fields) {
    bool found = false;
    std::apply([&](const auto&... f) { (void)((f.name == key && (f.push(state, s.*f.ptr), found = true)) || ...); }, fields);
    if (!found)
        lua_pushnil(state);
    return found;
}

template <typename S, typename... Fs>
bool applyField(lua_State* state, S& s, std::string_view key, int idx, const std::tuple<Fs...>& fields) {
    bool ok = false;
    std::apply(
      [&](const auto&... f) {
          (void)((f.name == key && [&] {
              if (auto v = f.apply(state, idx)) {
                  s.*f.ptr = *v;
                  ok = true;
              }
              return true;
          }()) || ...);
      },
      fields);
    return ok;
}

} // namespace lua::fields
