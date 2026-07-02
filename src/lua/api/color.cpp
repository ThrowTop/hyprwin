#include "lua/api/internal.hpp"

#include "lua/util/stack.hpp"

#include <cstring>
#include <optional>

namespace lua::color {
namespace {

    char kColorCtypeKey;

    bool isColorStorage(lua_State* state, int index) {
        if (lua_isuserdata(state, index)) {
            return true;
        }

        const int type = lua_type(state, index);
        const char* typeName = lua_typename(state, type);
        return typeName != nullptr && std::strcmp(typeName, "cdata") == 0;
    }

    std::optional<std::uint8_t> ReadByteField(lua_State* state, int index, const char* name, bool required = true) {
        lua_getfield(state, index, name);
        if (!lua_isnumber(state, -1)) {
            lua_pop(state, 1);
            return required ? std::nullopt : std::optional<std::uint8_t>{255};
        }

        const lua_Number number = lua_tonumber(state, -1);
        const auto value = static_cast<int>(number);
        lua_pop(state, 1);
        if (number != static_cast<lua_Number>(value) || value < 0 || value > 255) {
            return std::nullopt;
        }
        return static_cast<std::uint8_t>(value);
    }

    bool readColorFields(lua_State* state, int index, hw::Color& color) {
        const int value = util::absIndex(state, index);
        auto r = ReadByteField(state, value, "r");
        auto g = ReadByteField(state, value, "g");
        auto b = ReadByteField(state, value, "b");
        auto a = ReadByteField(state, value, "a", false);
        if (!r || !g || !b || !a) {
            return false;
        }

        color = hw::Color{*r, *g, *b, *a};
        return true;
    }

} // namespace

void registerApi(lua_State* state) {
    lua_newtable(state);
    lua_setglobal(state, "color");
}

void cacheColorCtype(lua_State* state) {
    lua_getglobal(state, "color");
    lua_getfield(state, -1, "_ct");
    lua_pushlightuserdata(state, &kColorCtypeKey);
    lua_pushvalue(state, -2);
    lua_rawset(state, LUA_REGISTRYINDEX);
    lua_pushnil(state);
    lua_setfield(state, -3, "_ct");
    lua_pop(state, 2);
}

void push(lua_State* state, const hw::Color& color) {
    lua_pushlightuserdata(state, &kColorCtypeKey);
    lua_rawget(state, LUA_REGISTRYINDEX);
    lua_pushinteger(state, color.r);
    lua_pushinteger(state, color.g);
    lua_pushinteger(state, color.b);
    lua_pushinteger(state, color.a);
    lua_call(state, 4, 1);
}

hw::Color check(lua_State* state, int index) {
    hw::Color color{};
    if (!read(state, index, color)) {
        luaL_error(state, "expected a color value");
    }
    return color;
}

bool read(lua_State* state, int index, hw::Color& color) {
    if (isColorStorage(state, index)) {
        if (readColorFields(state, index, color)) {
            return true;
        }

        if (lua_getmetatable(state, index) != 0) {
            lua_getfield(state, -1, "__is_hw_color");
            const bool isHwColor = lua_toboolean(state, -1) != 0;
            lua_pop(state, 2);
            if (isHwColor) {
                const void* data = lua_topointer(state, index);
                if (data == nullptr) {
                    return false;
                }
                color = *static_cast<const hw::Color*>(data);
                return true;
            }
        }
        return false;
    }

    if (lua_isstring(state, index)) {
        return hw::Color::FromHexRgb(util::toString(state, index), color);
    }

    return false;
}

} // namespace lua::color
