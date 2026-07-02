#include <lua.hpp>

#include "lua/util/stack.hpp"
#include "util/strings.hpp"
#include "win/native.hpp"

#include <shellapi.h>
#include <windows.h>

namespace lua::clipboard {

void registerApi(lua_State* state) {
    lua_newtable(state);

    util::setFn(state, "get", [](lua_State* s) -> int {
        if (!OpenClipboard(nullptr)) {
            lua_pushnil(s);
            return 1;
        }

        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (!h) {
            CloseClipboard();
            lua_pushnil(s);
            return 1;
        }

        const auto* wide = static_cast<const wchar_t*>(GlobalLock(h));
        if (!wide) {
            CloseClipboard();
            lua_pushnil(s);
            return 1;
        }

        const std::string utf8 = ::util::WideToUtf8(wide);
        GlobalUnlock(h);
        CloseClipboard();

        lua_pushlstring(s, utf8.data(), utf8.size());
        return 1;
    });

    util::setFn(state, "set", [](lua_State* s) -> int {
        std::size_t len = 0;
        const char* text = luaL_checklstring(s, 1, &len);
        win::SetClipboardText(::util::Utf8ToWide({text, len}));
        return 0;
    });

    util::setFn(state, "get_files", [](lua_State* s) -> int {
        if (!OpenClipboard(nullptr)) {
            lua_pushnil(s);
            return 1;
        }

        HANDLE h = GetClipboardData(CF_HDROP);
        if (!h) {
            CloseClipboard();
            lua_pushnil(s);
            return 1;
        }

        auto* drop = static_cast<HDROP>(h);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);

        lua_createtable(s, static_cast<int>(count), 0);
        for (UINT i = 0; i < count; ++i) {
            const UINT needed = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(static_cast<std::size_t>(needed), L'\0');
            DragQueryFileW(drop, i, path.data(), needed + 1);
            const std::string utf8 = ::util::WideToUtf8(path);
            lua_pushlstring(s, utf8.data(), utf8.size());
            lua_rawseti(s, -2, static_cast<int>(i) + 1);
        }

        CloseClipboard();
        return 1;
    });

    lua_setfield(state, -2, "clipboard");
}

} // namespace lua::clipboard
