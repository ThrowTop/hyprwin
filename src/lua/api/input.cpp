#include <lua.hpp>

#include "lua/util/stack.hpp"
#include "util/key_parse.hpp"
#include "util/strings.hpp"

#include <optional>
#include <vector>

namespace lua::input {
namespace {

    static std::optional<UINT> ModifierVk(std::string_view tok) noexcept {
        if (tok == "SUPER" || tok == "WIN" || tok == "LSUPER" || tok == "LWIN")
            return VK_LWIN;
        if (tok == "RSUPER" || tok == "RWIN")
            return VK_RWIN;
        if (tok == "SHIFT" || tok == "LSHIFT")
            return VK_LSHIFT;
        if (tok == "RSHIFT")
            return VK_RSHIFT;
        if (tok == "CTRL" || tok == "CONTROL" || tok == "LCTRL" || tok == "LCONTROL")
            return VK_LCONTROL;
        if (tok == "RCTRL" || tok == "RCONTROL")
            return VK_RCONTROL;
        if (tok == "ALT" || tok == "LALT")
            return VK_LMENU;
        if (tok == "RALT")
            return VK_RMENU;
        return std::nullopt;
    }

    static UINT ResolveVk(std::string_view name) noexcept {
        const std::string upper = ::util::UpperTrim(name);
        if (const auto mod = ModifierVk(upper))
            return *mod;
        return ::util::ParseVirtualKey(upper);
    }

    static HKL ForegroundHkl() {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd)
            return nullptr;
        DWORD tid = GetWindowThreadProcessId(hwnd, nullptr);
        return GetKeyboardLayout(tid);
    }

    static std::string HklToName(HKL hkl) {
        wchar_t buf[LOCALE_NAME_MAX_LENGTH];
        LCID lcid = MAKELCID(LOWORD(hkl), SORT_DEFAULT);
        if (LCIDToLocaleName(lcid, buf, LOCALE_NAME_MAX_LENGTH, 0) == 0)
            return {};
        return ::util::WideToUtf8(buf);
    }

    static std::vector<HKL> LoadedLayouts() {
        int count = GetKeyboardLayoutList(0, nullptr);
        if (count <= 0)
            return {};
        std::vector<HKL> list(static_cast<std::size_t>(count));
        GetKeyboardLayoutList(count, list.data());
        return list;
    }

    static void ActivateForForeground(HKL hkl) {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd)
            return;
        // DefWindowProc handles WM_INPUTLANGCHANGEREQUEST by calling ActivateKeyboardLayout
        // on the window's own thread -- the only reliable way to change layout cross-thread.
        PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)hkl);
    }

} // namespace

void registerApi(lua_State* state) {
    lua_newtable(state);

    util::setFn(state, "send", [](lua_State* s) -> int {
        std::size_t len = 0;
        const char* text = luaL_checklstring(s, 1, &len);

        UINT modifiers[8]{};
        UINT mod_count = 0;
        UINT key = 0;

        std::string_view remaining{text, len};
        while (true) {
            const std::size_t plus = remaining.find('+');
            const std::string token = ::util::UpperTrim(remaining.substr(0, plus));

            if (token.empty()) {
                luaL_error(s, "hw.input.send: empty token in '%s'", text);
                return 0;
            }

            if (const auto mod = ModifierVk(token)) {
                if (mod_count >= 8) {
                    luaL_error(s, "hw.input.send: too many modifiers");
                    return 0;
                }
                modifiers[mod_count++] = *mod;
            } else if (const UINT vk = ::util::ParseVirtualKey(token)) {
                if (key != 0) {
                    luaL_error(s, "hw.input.send: multiple non-modifier keys in '%s'", text);
                    return 0;
                }
                key = vk;
            } else {
                luaL_error(s, "hw.input.send: unknown key '%s'", token.c_str());
                return 0;
            }

            if (plus == std::string_view::npos)
                break;
            remaining.remove_prefix(plus + 1);
        }

        if (key == 0) {
            luaL_error(s, "hw.input.send: no main key in '%s'", text);
            return 0;
        }

        INPUT inputs[18]{};
        UINT count = 0;

        for (UINT i = 0; i < mod_count; ++i) {
            inputs[count].type = INPUT_KEYBOARD;
            inputs[count++].ki.wVk = static_cast<WORD>(modifiers[i]);
        }
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count++].ki.wVk = static_cast<WORD>(key);
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = static_cast<WORD>(key);
        inputs[count++].ki.dwFlags = KEYEVENTF_KEYUP;
        for (int i = static_cast<int>(mod_count) - 1; i >= 0; --i) {
            inputs[count].type = INPUT_KEYBOARD;
            inputs[count].ki.wVk = static_cast<WORD>(modifiers[i]);
            inputs[count++].ki.dwFlags = KEYEVENTF_KEYUP;
        }

        lua_pushboolean(s, SendInput(count, inputs, sizeof(INPUT)) == count ? 1 : 0);
        return 1;
    });

    util::setFn(state, "send_text", [](lua_State* s) -> int {
        std::size_t len = 0;
        const char* text = luaL_checklstring(s, 1, &len);
        const std::wstring wide = ::util::Utf8ToWide({text, len});
        if (wide.empty())
            return 0;

        std::vector<INPUT> inputs;
        inputs.reserve(wide.size() * 2);
        for (wchar_t ch : wide) {
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wScan = ch;
            in.ki.dwFlags = KEYEVENTF_UNICODE;
            inputs.push_back(in);
            in.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            inputs.push_back(in);
        }
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
        return 0;
    });

    util::setFn(state, "is_down", [](lua_State* s) -> int {
        const char* name = luaL_checkstring(s, 1);
        const UINT vk = ResolveVk(name);
        if (!vk) {
            luaL_error(s, "hw.input.is_down: unknown key '%s'", name);
            return 0;
        }
        lua_pushboolean(s, (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0 ? 1 : 0);
        return 1;
    });

    util::setFn(state, "get_toggle", [](lua_State* s) -> int {
        const char* name = luaL_checkstring(s, 1);
        const UINT vk = ResolveVk(name);
        if (!vk) {
            luaL_error(s, "hw.input.get_toggle: unknown key '%s'", name);
            return 0;
        }
        lua_pushboolean(s, (GetKeyState(static_cast<int>(vk)) & 1) != 0 ? 1 : 0);
        return 1;
    });

    util::setFn(state, "toggle", [](lua_State* s) -> int {
        const char* name = luaL_checkstring(s, 1);
        const UINT vk = ResolveVk(name);
        if (!vk) {
            luaL_error(s, "hw.input.toggle: unknown key '%s'", name);
            return 0;
        }
        INPUT inputs[2]{};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = static_cast<WORD>(vk);
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = static_cast<WORD>(vk);
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
        return 0;
    });

    util::setFn(state, "lang", [](lua_State* s) -> int {
        HKL hkl = ForegroundHkl();
        if (!hkl) {
            lua_pushnil(s);
            return 1;
        }
        const std::string name = HklToName(hkl);
        if (name.empty()) {
            lua_pushnil(s);
            return 1;
        }
        lua_pushlstring(s, name.c_str(), name.size());
        return 1;
    });

    util::setFn(state, "lang_list", [](lua_State* s) -> int {
        const auto layouts = LoadedLayouts();
        lua_createtable(s, static_cast<int>(layouts.size()), 0);
        int idx = 1;
        for (HKL hkl : layouts) {
            const std::string name = HklToName(hkl);
            if (name.empty())
                continue;
            lua_pushlstring(s, name.c_str(), name.size());
            lua_rawseti(s, -2, idx++);
        }
        return 1;
    });

    util::setFn(state, "set_lang", [](lua_State* s) -> int {
        std::size_t len = 0;
        const char* name = luaL_checklstring(s, 1, &len);
        const std::string_view target{name, len};
        for (HKL hkl : LoadedLayouts()) {
            if (HklToName(hkl) == target) {
                ActivateForForeground(hkl);
                return 0;
            }
        }
        luaL_error(s, "hw.input.set_lang: layout '%s' not found", name);
        return 0;
    });

    util::setFn(state, "next_lang", [](lua_State*) -> int {
        const auto layouts = LoadedLayouts();
        if (layouts.empty())
            return 0;
        HKL current = ForegroundHkl();
        int idx = 0;
        for (int i = 0; i < static_cast<int>(layouts.size()); ++i) {
            if (layouts[i] == current) {
                idx = i;
                break;
            }
        }
        ActivateForForeground(layouts[(idx + 1) % static_cast<int>(layouts.size())]);
        return 0;
    });

    lua_setfield(state, -2, "input");
}

} // namespace lua::input
