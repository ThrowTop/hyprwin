#include "lua/api/internal.hpp"

#include "lua/util/stack.hpp"
#include "util/strings.hpp"
#include "win/native.hpp"

#include <algorithm>

namespace lua::monitor {
namespace {

    constexpr char kMonitorMetatable[] = "HW.Monitor";

    win::MonitorInfo* CheckMonitor(lua_State* state, int index) {
        return static_cast<win::MonitorInfo*>(luaL_checkudata(state, index, kMonitorMetatable));
    }

    void PushMonitor(lua_State* state, const win::MonitorInfo& info) {
        auto* ref = static_cast<win::MonitorInfo*>(lua_newuserdata(state, sizeof(win::MonitorInfo)));
        *ref = info;
        luaL_getmetatable(state, kMonitorMetatable);
        lua_setmetatable(state, -2);
    }

    int MonitorSetResolution(lua_State* state) {
        auto* ref = CheckMonitor(state, 1);
        const int w = static_cast<int>(luaL_checkinteger(state, 2));
        const int h = static_cast<int>(luaL_checkinteger(state, 3));
        const int hz = static_cast<int>(luaL_checkinteger(state, 4));
        lua_pushboolean(state, win::SetMonitorResolution(ref->name, w, h, hz));
        return 1;
    }

    int MonitorListModes(lua_State* state) {
        auto* ref = CheckMonitor(state, 1);
        lua_newtable(state);
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        int i = 1;
        for (DWORD idx = 0; EnumDisplaySettingsW(ref->name, idx, &mode) != FALSE; ++idx) {
            lua_createtable(state, 0, 3);
            util::setIntegerField(state, "width", static_cast<lua_Integer>(mode.dmPelsWidth));
            util::setIntegerField(state, "height", static_cast<lua_Integer>(mode.dmPelsHeight));
            util::setIntegerField(state, "hz", static_cast<lua_Integer>(mode.dmDisplayFrequency));
            lua_rawseti(state, -2, i++);
        }
        return 1;
    }

    int MonitorMoveWindow(lua_State* state) {
        auto* mon = CheckMonitor(state, 1);
        const HWND hwnd = window::check(state, 2);

        RECT visual{};
        if (!win::GetVisualWindowRect(hwnd, visual)) {
            lua_pushboolean(state, 0);
            return 1;
        }

        const win::MonitorInfo src = win::GetMonitorForWindow(hwnd);
        const LONG w = visual.right - visual.left;
        const LONG h = visual.bottom - visual.top;
        LONG left = mon->work_area.left + (visual.left - src.work_area.left);
        LONG top = mon->work_area.top + (visual.top - src.work_area.top);
        left = std::max(left, mon->work_area.left);
        top = std::max(top, mon->work_area.top);

        const RECT target{left, top, left + w, top + h};
        lua_pushboolean(state, win::MoveWindowToVisualRect(hwnd, target));
        return 1;
    }

    int MonitorIndex(lua_State* state) {
        auto* ref = CheckMonitor(state, 1);
        const std::string key = util::toString(state, 2);

        if (key == "rect") {
            vec::push(state, ::vec::i4::FromWin32(ref->rect));
            return 1;
        }
        if (key == "work_area") {
            vec::push(state, ::vec::i4::FromWin32(ref->work_area));
            return 1;
        }
        if (key == "is_primary") {
            lua_pushboolean(state, ref->is_primary ? 1 : 0);
            return 1;
        }
        if (key == "width") {
            lua_pushinteger(state, ref->rect.right - ref->rect.left);
            return 1;
        }
        if (key == "height") {
            lua_pushinteger(state, ref->rect.bottom - ref->rect.top);
            return 1;
        }
        if (key == "name") {
            const std::string n = ::util::WideToUtf8(ref->name);
            util::pushString(state, n);
            return 1;
        }

        if (key == "dpi" || key == "scale") {
            UINT dpix = 96, dpiy = 96;
            win::GetMonitorDpi(ref->handle, dpix, dpiy);
            if (key == "scale") {
                lua_pushnumber(state, static_cast<lua_Number>(dpix) / 96.0);
            } else {
                vec::push(state, ::vec::i2{static_cast<int>(dpix), static_cast<int>(dpiy)});
            }
            return 1;
        }

        if (key == "hz" || key == "bit_depth") {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            EnumDisplaySettingsW(ref->name, ENUM_CURRENT_SETTINGS, &mode);
            lua_pushinteger(state, static_cast<lua_Integer>(key == "hz" ? mode.dmDisplayFrequency : mode.dmBitsPerPel));
            return 1;
        }

        if (key == "set_resolution") {
            lua_pushcfunction(state, MonitorSetResolution);
            return 1;
        }
        if (key == "list_modes") {
            lua_pushcfunction(state, MonitorListModes);
            return 1;
        }
        if (key == "move_window") {
            lua_pushcfunction(state, MonitorMoveWindow);
            return 1;
        }

        lua_pushnil(state);
        return 1;
    }

    int MonitorToString(lua_State* state) {
        auto* ref = CheckMonitor(state, 1);
        const std::string n = ::util::WideToUtf8(ref->name);
        lua_pushfstring(state, "HW.Monitor(%s%s)", n.c_str(), ref->is_primary ? ", primary" : "");
        return 1;
    }

    void EnsureMonitorMetatable(lua_State* state) {
        util::ensureMetatable(state, kMonitorMetatable, [](lua_State* s) {
            util::setFn(s, "__index", MonitorIndex);
            util::setFn(s, "__tostring", MonitorToString);
        });
    }

    int MonitorPrimary(lua_State* state) {
        PushMonitor(state, win::GetPrimaryMonitor());
        return 1;
    }

    int MonitorList(lua_State* state) {
        const std::vector<win::MonitorInfo> monitors = win::GetMonitors();
        lua_newtable(state);
        for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
            PushMonitor(state, monitors[i]);
            lua_rawseti(state, -2, i + 1);
        }
        return 1;
    }

    int MonitorAt(lua_State* state) {
        const auto p = vec::checkPoint(state, 1);
        PushMonitor(state, win::GetMonitorAtPoint(static_cast<LONG>(p.x), static_cast<LONG>(p.y)));
        return 1;
    }

    int MonitorForWindow(lua_State* state) {
        PushMonitor(state, win::GetMonitorForWindow(window::check(state, 1)));
        return 1;
    }

    int WorkArea(lua_State* state) {
        RECT workArea{};
        bool ok = false;
        if (lua_gettop(state) >= 1 && !lua_isnil(state, 1)) {
            ok = win::GetWorkAreaForWindow(window::check(state, 1), workArea);
        } else {
            ok = win::GetPrimaryWorkArea(workArea);
        }
        if (!ok) {
            lua_pushnil(state);
            return 1;
        }
        vec::push(state, ::vec::i4::FromWin32(workArea));
        return 1;
    }

} // namespace

void registerApi(lua_State* state) {
    EnsureMonitorMetatable(state);

    util::setTableField(state, "mon", [](lua_State* s) {
        util::setFn(s, "primary", MonitorPrimary);
        util::setFn(s, "list", MonitorList);
        util::setFn(s, "at", MonitorAt);
        util::setFn(s, "for_window", MonitorForWindow);
        util::setFn(s, "work_area", WorkArea);
    });
}

} // namespace lua::monitor
