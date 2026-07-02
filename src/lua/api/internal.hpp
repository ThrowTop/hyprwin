#pragma once

#include <lua.hpp>
#include <windows.h>

#include "lua/context.hpp"
#include "util/geometry.hpp"
#include "util/strings.hpp"

namespace lua::api {

void registerAll(lua_State* state, Context& context);

} // namespace lua::api

namespace lua::color {

void registerApi(lua_State* state);
void cacheColorCtype(lua_State* state);
void push(lua_State* state, const hw::Color& color);
hw::Color check(lua_State* state, int index);
bool read(lua_State* state, int index, hw::Color& color);

} // namespace lua::color

namespace lua::vec {

void cacheConstructors(lua_State* state);
void push(lua_State* state, ::vec::i2 point);
void push(lua_State* state, ::vec::i4 rect);
::vec::i2 checkPoint(lua_State* state, int index);
::vec::i4 checkRect(lua_State* state, int index);

} // namespace lua::vec

namespace lua::settings {

void registerApi(lua_State* state);
void storeHwImpl(lua_State* state);
int hwIndex(lua_State* state);
int hwNewIndex(lua_State* state);

} // namespace lua::settings

namespace lua::window {

void registerApi(lua_State* state);
void push(lua_State* state, HWND hwnd);
HWND check(lua_State* state, int index);

} // namespace lua::window

namespace lua::map {

template <auto Check, auto Fn, auto... Extra>
int Bool(lua_State* state) {
    lua_pushboolean(state, Fn(Check(state, 1), Extra...));
    return 1;
}

template <auto Check, auto Fn>
int Str(lua_State* state) {
    const std::string s = ::util::WideToUtf8(Fn(Check(state, 1)));
    lua_pushlstring(state, s.data(), s.size());
    return 1;
}

} // namespace lua::map

namespace lua::core {
void registerApi(lua_State* state);
} // namespace lua::core

namespace lua::monitor {
void registerApi(lua_State* state);
} // namespace lua::monitor

namespace lua::system {
void registerApi(lua_State* state);
} // namespace lua::system

namespace lua::fs {
void registerApi(lua_State* state);
} // namespace lua::fs

namespace lua::input {
void registerApi(lua_State* state);
} // namespace lua::input

namespace lua::clipboard {
void registerApi(lua_State* state);
} // namespace lua::clipboard

namespace lua::mouse_api {
void registerApi(lua_State* state);
} // namespace lua::mouse_api

namespace lua::audio {
void registerApi(lua_State* state);
} // namespace lua::audio
