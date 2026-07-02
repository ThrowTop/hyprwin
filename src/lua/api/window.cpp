#include "lua/api/internal.hpp"

#include "lua/util/stack.hpp"
#include "util/strings.hpp"
#include "win/native.hpp"

#include <cstdint>
#include <vector>

namespace lua::window {
namespace {
    constexpr char kWindowMetatable[] = "HW.Window";

    struct WindowRef {
        HWND hwnd = nullptr;
        DWORD pid = 0;
    };

    WindowRef* CheckWindowRef(lua_State* state, int index) {
        return static_cast<WindowRef*>(luaL_checkudata(state, index, kWindowMetatable));
    }

    int WindowAtCursor(lua_State* state);
    int WindowDump(lua_State* state);
    int WindowFocused(lua_State* state);
    int WindowGetFullscreen(lua_State* state);
    int WindowList(lua_State* state);
    int WindowPid(lua_State* state);
    int WindowRect(lua_State* state);
    int WindowResponsive(lua_State* state);
    int WindowSetRect(lua_State* state);
    int WindowToString(lua_State* state);

    void EnsureWindowMetatable(lua_State* state) {
        util::ensureMetatable(state, kWindowMetatable, [](lua_State* s) {
            lua_pushvalue(s, -1);
            lua_setfield(s, -2, "__index");

            util::setFn(s, "__tostring", WindowToString);
            util::setFn(s, "at_cursor", WindowAtCursor);
            util::setFn(s, "class", map::Str<window::check, win::GetWindowClass>);
            util::setFn(s, "close", map::Bool<window::check, win::PostCloseWindow>);
            util::setFn(s, "dump", WindowDump);
            util::setFn(s, "focus", [](lua_State* inner) -> int {
                win::FocusWindow(window::check(inner, 1));
                return 0;
            });
            util::setFn(s, "focused", WindowFocused);
            util::setFn(s, "get_fullscreen", WindowGetFullscreen);
            util::setFn(s, "get_maximized", map::Bool<window::check, win::GetMaximized>);
            util::setFn(s, "get_minimized", map::Bool<window::check, win::GetMinimized>);
            util::setFn(s, "get_resizable", map::Bool<window::check, win::GetResizable>);
            util::setFn(s, "kill", map::Bool<window::check, win::KillWindowProcess>);
            util::setFn(s, "list", WindowList);
            util::setFn(s, "maximize", map::Bool<window::check, win::MaximizeWindow>);
            util::setFn(s, "minimize", map::Bool<window::check, win::MinimizeWindow>);
            util::setFn(s, "restore", map::Bool<window::check, win::RestoreWindow>);
            util::setFn(s, "pid", WindowPid);
            util::setFn(s, "pname", map::Str<window::check, win::GetProcessName>);
            util::setFn(s, "visual_rect", WindowRect);
            util::setFn(s, "responsive", WindowResponsive);
            util::setFn(s, "set_visual_rect", WindowSetRect);
            util::setFn(s, "title", map::Str<window::check, win::GetWindowTitle>);
        });
    }

    RECT CheckRect(lua_State* state, int index) {
        return vec::checkRect(state, index).ToWin32();
    }
} // namespace

void registerApi(lua_State* state) {
    EnsureWindowMetatable(state);

    lua_newtable(state);
    util::setFn(state, "at_cursor", WindowAtCursor);
    util::setFn(state, "focused", WindowFocused);
    util::setFn(state, "list", WindowList);
    lua_setfield(state, -2, "window");
}

void push(lua_State* state, HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        lua_pushnil(state);
        return;
    }

    auto* ref = static_cast<WindowRef*>(lua_newuserdata(state, sizeof(WindowRef)));
    ref->hwnd = hwnd;
    GetWindowThreadProcessId(hwnd, &ref->pid);
    luaL_getmetatable(state, kWindowMetatable);
    lua_setmetatable(state, -2);
}

HWND check(lua_State* state, int index) {
    WindowRef* ref = CheckWindowRef(state, index);
    DWORD pid = 0;
    if (!ref->hwnd || !GetWindowThreadProcessId(ref->hwnd, &pid) || pid != ref->pid) {
        luaL_argerror(state, index, "stale window");
    }
    return ref->hwnd;
}

namespace {
    int WindowAtCursor(lua_State* state) {
        push(state, win::GetFilteredWindowAtCursor());
        return 1;
    }

    int WindowFocused(lua_State* state) {
        push(state, win::GetForegroundWindowChecked());
        return 1;
    }

    int WindowGetFullscreen(lua_State* state) {
        const HWND hwnd = check(state, 1);
        RECT rawRect{};
        lua_pushboolean(state, win::GetRawWindowRect(hwnd, rawRect) && win::GetBorderlessFullscreen(hwnd, rawRect));
        return 1;
    }

    int WindowList(lua_State* state) {
        const bool has_filter = lua_gettop(state) >= 1 && lua_isfunction(state, 1);

        std::vector<HWND> hwnds = win::GetTopLevelWindows();
        lua_newtable(state);
        const int tbl = lua_gettop(state);
        int count = 0;

        for (HWND hwnd : hwnds) {
            push(state, hwnd);
            if (lua_isnil(state, -1)) {
                lua_pop(state, 1);
                continue;
            }

            if (has_filter) {
                lua_pushvalue(state, 1);
                lua_pushvalue(state, -2);
                lua_call(state, 1, 1);
                const bool keep = lua_toboolean(state, -1) != 0;
                lua_pop(state, 1);
                if (!keep) {
                    lua_pop(state, 1);
                    continue;
                }
            }

            lua_rawseti(state, tbl, ++count);
        }

        return 1;
    }

    int WindowPid(lua_State* state) {
        const DWORD pid = win::GetProcessId(check(state, 1));
        if (pid == 0) {
            lua_pushnil(state);
            return 1;
        }
        lua_pushinteger(state, static_cast<lua_Integer>(pid));
        return 1;
    }

    int WindowRect(lua_State* state) {
        RECT rect{};
        if (!win::GetVisualWindowRect(check(state, 1), rect)) {
            lua_pushnil(state);
            return 1;
        }
        vec::push(state, ::vec::i4::FromWin32(rect));
        return 1;
    }

    int WindowResponsive(lua_State* state) {
        const HWND hwnd = check(state, 1);
        const DWORD ms = lua_isnoneornil(state, 2) ? 200 : static_cast<DWORD>(luaL_checkinteger(state, 2));
        lua_pushboolean(state, win::IsWindowResponsive(hwnd, ms));
        return 1;
    }

    int WindowSetRect(lua_State* state) {
        const HWND hwnd = check(state, 1);
        const RECT rect = CheckRect(state, 2);
        lua_pushboolean(state, win::MoveWindowToVisualRect(hwnd, rect));
        return 1;
    }

    int WindowToString(lua_State* state) {
        const HWND hwnd = check(state, 1);
        lua_pushfstring(state, "HW.Window: %p", hwnd);
        return 1;
    }

    int WindowDump(lua_State* state) {
        const HWND hwnd = check(state, 1);

        const std::string name = ::util::WideToUtf8(win::GetProcessName(hwnd));
        const std::string title = ::util::WideToUtf8(win::GetWindowTitle(hwnd));
        const std::string cls = ::util::WideToUtf8(win::GetWindowClass(hwnd));
        const DWORD pid = win::GetProcessId(hwnd);

        RECT raw{}, visual{};
        const bool hasRaw = win::GetRawWindowRect(hwnd, raw);
        const bool hasVisual = win::GetVisualWindowRect(hwnd, visual);
        const bool fs = hasRaw && win::GetBorderlessFullscreen(hwnd, raw);

        lua_createtable(state, 0, 11);
        lua_pushinteger(state, static_cast<lua_Integer>(reinterpret_cast<std::uintptr_t>(hwnd)));
        lua_setfield(state, -2, "hwnd");
        lua_pushlstring(state, name.data(), name.size());
        lua_setfield(state, -2, "process");
        lua_pushlstring(state, title.data(), title.size());
        lua_setfield(state, -2, "title");
        lua_pushlstring(state, cls.data(), cls.size());
        lua_setfield(state, -2, "class");
        if (pid != 0) {
            lua_pushinteger(state, static_cast<lua_Integer>(pid));
            lua_setfield(state, -2, "pid");
        }
        if (hasRaw) {
            vec::push(state, ::vec::i4::FromWin32(raw));
            lua_setfield(state, -2, "raw_rect");
        }
        if (hasVisual) {
            vec::push(state, ::vec::i4::FromWin32(visual));
            lua_setfield(state, -2, "visual_rect");
        }
        lua_pushboolean(state, win::GetMaximized(hwnd));
        lua_setfield(state, -2, "maximized");
        lua_pushboolean(state, win::GetMinimized(hwnd));
        lua_setfield(state, -2, "minimized");
        lua_pushboolean(state, fs);
        lua_setfield(state, -2, "fullscreen");
        lua_pushboolean(state, win::GetResizable(hwnd));
        lua_setfield(state, -2, "resizable");

        return 1;
    }
} // namespace
} // namespace lua::window
