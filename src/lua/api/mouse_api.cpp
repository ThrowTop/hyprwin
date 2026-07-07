#include "lua/api/internal.hpp"

#include "lua/util/stack.hpp"
#include "win/native.hpp"

#include <algorithm>

namespace lua::mouse_api {
namespace {
    static DWORD BtnDown(std::string_view btn) noexcept {
        if (btn == "left")
            return MOUSEEVENTF_LEFTDOWN;
        if (btn == "right")
            return MOUSEEVENTF_RIGHTDOWN;
        if (btn == "middle")
            return MOUSEEVENTF_MIDDLEDOWN;
        return 0;
    }

    static DWORD BtnUp(std::string_view btn) noexcept {
        if (btn == "left")
            return MOUSEEVENTF_LEFTUP;
        if (btn == "right")
            return MOUSEEVENTF_RIGHTUP;
        if (btn == "middle")
            return MOUSEEVENTF_MIDDLEUP;
        return 0;
    }

    // Normalize screen coordinates to MOUSEEVENTF_ABSOLUTE range [0, 65535]
    static void AbsCoords(LONG x, LONG y, LONG& ax, LONG& ay) noexcept {
        const ::vec::i4 vs = win::GetVirtualScreenBounds();
        const int vw = vs.Width();
        const int vh = vs.Height();
        ax = static_cast<LONG>(std::clamp(static_cast<long long>(x - vs.x) * 65535 / (vw > 1 ? vw - 1 : 1), 0LL, 65535LL));
        ay = static_cast<LONG>(std::clamp(static_cast<long long>(y - vs.y) * 65535 / (vh > 1 ? vh - 1 : 1), 0LL, 65535LL));
    }

    int MousePos(lua_State* state) {
        POINT pt{};
        GetCursorPos(&pt);
        vec::push(state, ::vec::i2{static_cast<int>(pt.x), static_cast<int>(pt.y)});
        return 1;
    }

    int MouseMove(lua_State* state) {
        const auto p = vec::checkPoint(state, 1);
        SetCursorPos(p.x, p.y);
        return 0;
    }

    int MouseClick(lua_State* state) {
        const char* btn = luaL_checkstring(state, 1);
        const DWORD down = BtnDown(btn);
        const DWORD up = BtnUp(btn);
        if (!down) {
            luaL_error(state, "hw.mouse.click: unknown button '%s'", btn);
            return 0;
        }

        if (!lua_isnoneornil(state, 2)) {
            const auto p = vec::checkPoint(state, 2);
            LONG ax{}, ay{};
            AbsCoords(static_cast<LONG>(p.x), static_cast<LONG>(p.y), ax, ay);

            INPUT inputs[3]{};
            inputs[0].type = INPUT_MOUSE;
            inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
            inputs[0].mi.dx = ax;
            inputs[0].mi.dy = ay;
            inputs[1].type = INPUT_MOUSE;
            inputs[1].mi.dwFlags = down;
            inputs[2].type = INPUT_MOUSE;
            inputs[2].mi.dwFlags = up;
            SendInput(3, inputs, sizeof(INPUT));
        } else {
            INPUT inputs[2]{};
            inputs[0].type = INPUT_MOUSE;
            inputs[0].mi.dwFlags = down;
            inputs[1].type = INPUT_MOUSE;
            inputs[1].mi.dwFlags = up;
            SendInput(2, inputs, sizeof(INPUT));
        }
        return 0;
    }

    int MouseDown(lua_State* state) {
        const char* btn = luaL_checkstring(state, 1);
        const DWORD flag = BtnDown(btn);
        if (!flag) {
            luaL_error(state, "hw.mouse.down: unknown button '%s'", btn);
            return 0;
        }
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = flag;
        SendInput(1, &in, sizeof(INPUT));
        return 0;
    }

    int MouseUp(lua_State* state) {
        const char* btn = luaL_checkstring(state, 1);
        const DWORD flag = BtnUp(btn);
        if (!flag) {
            luaL_error(state, "hw.mouse.up: unknown button '%s'", btn);
            return 0;
        }
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = flag;
        SendInput(1, &in, sizeof(INPUT));
        return 0;
    }

    int MouseScroll(lua_State* state) {
        const int delta = static_cast<int>(luaL_checkinteger(state, 1));
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        in.mi.mouseData = static_cast<DWORD>(delta * WHEEL_DELTA);
        SendInput(1, &in, sizeof(INPUT));
        return 0;
    }

} // namespace

void registerApi(lua_State* state) {
    util::setTableField(state, "mouse", [](lua_State* s) {
        util::setFn(s, "pos", MousePos);
        util::setFn(s, "move", MouseMove);
        util::setFn(s, "click", MouseClick);
        util::setFn(s, "down", MouseDown);
        util::setFn(s, "up", MouseUp);
        util::setFn(s, "scroll", MouseScroll);
    });
}
} // namespace lua::mouse_api
