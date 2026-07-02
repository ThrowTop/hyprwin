#include "lua/api/internal.hpp"

namespace lua::vec {
namespace {
    char kPointKey;
    char kRectKey;
} // namespace

void cacheConstructors(lua_State* state) {
    lua_pushlightuserdata(state, &kPointKey);
    lua_getglobal(state, "point");
    lua_rawset(state, LUA_REGISTRYINDEX);

    lua_pushlightuserdata(state, &kRectKey);
    lua_getglobal(state, "rect");
    lua_rawset(state, LUA_REGISTRYINDEX);
}

void push(lua_State* state, ::vec::i2 p) {
    lua_pushlightuserdata(state, &kPointKey);
    lua_rawget(state, LUA_REGISTRYINDEX);
    lua_pushinteger(state, p.x);
    lua_pushinteger(state, p.y);
    lua_call(state, 2, 1);
}

void push(lua_State* state, ::vec::i4 r) {
    lua_pushlightuserdata(state, &kRectKey);
    lua_rawget(state, LUA_REGISTRYINDEX);
    lua_pushinteger(state, r.x);
    lua_pushinteger(state, r.y);
    lua_pushinteger(state, r.x2);
    lua_pushinteger(state, r.y2);
    lua_call(state, 4, 1);
}

::vec::i2 checkPoint(lua_State* state, int index) {
    luaL_checktype(state, index, LUA_TTABLE);
    if (index < 0)
        index = lua_gettop(state) + 1 + index;
    lua_getfield(state, index, "x");
    lua_getfield(state, index, "y");
    ::vec::i2 r{
      static_cast<int>(luaL_checkinteger(state, -2)),
      static_cast<int>(luaL_checkinteger(state, -1)),
    };
    lua_pop(state, 2);
    return r;
}

::vec::i4 checkRect(lua_State* state, int index) {
    luaL_checktype(state, index, LUA_TTABLE);
    if (index < 0)
        index = lua_gettop(state) + 1 + index;
    lua_getfield(state, index, "x");
    lua_getfield(state, index, "y");
    lua_getfield(state, index, "x2");
    lua_getfield(state, index, "y2");
    ::vec::i4 r{
      static_cast<int>(luaL_checkinteger(state, -4)),
      static_cast<int>(luaL_checkinteger(state, -3)),
      static_cast<int>(luaL_checkinteger(state, -2)),
      static_cast<int>(luaL_checkinteger(state, -1)),
    };
    lua_pop(state, 4);
    return r;
}

} // namespace lua::vec
