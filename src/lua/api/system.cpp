#include "lua/api/internal.hpp"

#include "log/log.hpp"
#include "lua/util/stack.hpp"
#include "util/strings.hpp"

#include <string>

namespace lua::system {
namespace {

    int Lock(lua_State* state) {
        lua_pushboolean(state, LockWorkStation() != 0);
        return 1;
    }

    int Env(lua_State* state) {
        const char* name = luaL_checkstring(state, 1);
        const std::wstring wname = ::util::Utf8ToWide(name);
        const DWORD needed = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
        if (needed == 0) {
            lua_pushnil(state);
            return 1;
        }
        std::wstring buf(static_cast<std::size_t>(needed), L'\0');
        const DWORD actual = GetEnvironmentVariableW(wname.c_str(), buf.data(), needed);
        if (actual == 0 || actual >= needed) {
            lua_pushnil(state);
            return 1;
        }
        buf.resize(actual);
        const std::string utf8 = ::util::WideToUtf8(buf);
        util::pushString(state, utf8);
        return 1;
    }

    int Username(lua_State* state) {
        wchar_t buf[256];
        DWORD len = static_cast<DWORD>(std::size(buf));
        if (!GetUserNameW(buf, &len)) {
            lua_pushnil(state);
            return 1;
        }
        const std::string utf8 = ::util::WideToUtf8(std::wstring_view(buf, len > 0 ? len - 1 : 0));
        util::pushString(state, utf8);
        return 1;
    }

    int DebugConsole(lua_State* state) {
        if (lua_gettop(state) == 0 || lua_isnil(state, 1)) {
            lua_pushboolean(state, logging::viewer_open());
            return 1;
        }

        if (lua_isboolean(state, 1)) {
            const bool enabled = lua_toboolean(state, 1) != 0;
            if (enabled) {
                lua_pushboolean(state, logging::open_viewer());
            } else {
                logging::close_viewer();
                lua_pushboolean(state, 1);
            }
            return 1;
        }

        const std::string action = util::toString(state, 1);
        if (action == "toggle") {
            if (logging::viewer_open()) {
                logging::close_viewer();
                lua_pushboolean(state, 0);
            } else {
                lua_pushboolean(state, logging::open_viewer());
            }
            return 1;
        }

        return luaL_error(state, "hw.sys.debug_console: expected boolean, nil, or 'toggle'");
    }

    int SendNotifyMsg(lua_State* state) {
        const auto hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(luaL_checkinteger(state, 1)));
        const UINT msg = static_cast<UINT>(luaL_checkinteger(state, 2));
        const auto wp = static_cast<WPARAM>(luaL_optinteger(state, 3, 0));
        const LPARAM lp = static_cast<LPARAM>(luaL_optinteger(state, 4, 0));

        SendNotifyMessageW(hwnd, msg, wp, lp);
        return 0;
    }

    // Normalizes / to \ and returns the HKEY root + remaining subkey.
    static bool ParseRegKey(std::string_view raw, HKEY& root, std::wstring& subkey) {
        std::string s(raw);
        for (char& c : s) {
            if (c == '/')
                c = '\\';
        }

        const auto sep = s.find('\\');
        const std::string hive = (sep == std::string::npos) ? s : s.substr(0, sep);
        const std::string_view rest = (sep == std::string::npos) ? "" : std::string_view(s).substr(sep + 1);

        if (hive == "HKCU" || hive == "HKEY_CURRENT_USER")
            root = HKEY_CURRENT_USER;
        else if (hive == "HKLM" || hive == "HKEY_LOCAL_MACHINE")
            root = HKEY_LOCAL_MACHINE;
        else if (hive == "HKCR" || hive == "HKEY_CLASSES_ROOT")
            root = HKEY_CLASSES_ROOT;
        else if (hive == "HKU" || hive == "HKEY_USERS")
            root = HKEY_USERS;
        else if (hive == "HKCC" || hive == "HKEY_CURRENT_CONFIG")
            root = HKEY_CURRENT_CONFIG;
        else
            return false;

        subkey = ::util::Utf8ToWide(rest);
        return true;
    }

    int RegGet(lua_State* state) {
        const char* key_str = luaL_checkstring(state, 1);
        const bool has_name = !lua_isnoneornil(state, 2);
        std::wstring value_name;
        if (has_name)
            value_name = ::util::Utf8ToWide(luaL_checkstring(state, 2));

        HKEY root{};
        std::wstring subkey;
        if (!ParseRegKey(key_str, root, subkey)) {
            return luaL_error(state, "hw.sys.reg_get: unknown hive in '%s'", key_str);
        }

        HKEY hk{};
        if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS) {
            lua_pushnil(state);
            return 1;
        }

        DWORD type{};
        DWORD bytes{};
        const wchar_t* vname = has_name ? value_name.c_str() : nullptr;
        if (RegQueryValueExW(hk, vname, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS) {
            RegCloseKey(hk);
            lua_pushnil(state);
            return 1;
        }

        if (type == REG_SZ || type == REG_EXPAND_SZ) {
            std::wstring buf(bytes / sizeof(wchar_t), L'\0');
            if (RegQueryValueExW(hk, vname, nullptr, nullptr, reinterpret_cast<BYTE*>(buf.data()), &bytes) != ERROR_SUCCESS) {
                RegCloseKey(hk);
                lua_pushnil(state);
                return 1;
            }
            // strip embedded null terminator if present
            if (!buf.empty() && buf.back() == L'\0')
                buf.pop_back();
            const std::string utf8 = ::util::WideToUtf8(buf);
            util::pushString(state, utf8);
        } else if (type == REG_DWORD) {
            DWORD val{};
            bytes = sizeof(val);
            if (RegQueryValueExW(hk, vname, nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &bytes) != ERROR_SUCCESS) {
                RegCloseKey(hk);
                lua_pushnil(state);
                return 1;
            }
            lua_pushinteger(state, static_cast<lua_Integer>(val));
        } else if (type == REG_QWORD) {
            UINT64 val{};
            bytes = sizeof(val);
            if (RegQueryValueExW(hk, vname, nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &bytes) != ERROR_SUCCESS) {
                RegCloseKey(hk);
                lua_pushnil(state);
                return 1;
            }
            lua_pushinteger(state, static_cast<lua_Integer>(val));
        } else {
            lua_pushnil(state);
        }

        RegCloseKey(hk);
        return 1;
    }

    int RegSet(lua_State* state) {
        const char* key_str = luaL_checkstring(state, 1);
        std::wstring vname = ::util::Utf8ToWide(luaL_checkstring(state, 2));

        HKEY root{};
        std::wstring subkey;
        if (!ParseRegKey(key_str, root, subkey)) {
            return luaL_error(state, "hw.sys.reg_set: unknown hive in '%s'", key_str);
        }

        HKEY hk{};
        if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hk, nullptr) != ERROR_SUCCESS) {
            lua_pushboolean(state, 0);
            return 1;
        }

        LONG rc = ERROR_SUCCESS;
        if (lua_type(state, 3) == LUA_TSTRING) {
            std::size_t len{};
            const char* s = luaL_checklstring(state, 3, &len);
            const std::wstring wval = ::util::Utf8ToWide({s, len});
            const DWORD bytes = static_cast<DWORD>((wval.size() + 1) * sizeof(wchar_t));
            rc = RegSetValueExW(hk, vname.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(wval.c_str()), bytes);
        } else {
            const DWORD val = static_cast<DWORD>(luaL_checkinteger(state, 3));
            rc = RegSetValueExW(hk, vname.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
        }

        RegCloseKey(hk);
        lua_pushboolean(state, rc == ERROR_SUCCESS ? 1 : 0);
        return 1;
    }

    int RegExists(lua_State* state) {
        const char* key_str = luaL_checkstring(state, 1);
        const bool has_name = !lua_isnoneornil(state, 2);
        std::wstring vname;
        if (has_name)
            vname = ::util::Utf8ToWide(luaL_checkstring(state, 2));

        HKEY root{};
        std::wstring subkey;
        if (!ParseRegKey(key_str, root, subkey)) {
            return luaL_error(state, "hw.sys.reg_exists: unknown hive in '%s'", key_str);
        }

        HKEY hk{};
        if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS) {
            lua_pushboolean(state, 0);
            return 1;
        }

        if (has_name) {
            const LONG rc = RegQueryValueExW(hk, vname.c_str(), nullptr, nullptr, nullptr, nullptr);
            lua_pushboolean(state, rc == ERROR_SUCCESS ? 1 : 0);
        } else {
            lua_pushboolean(state, 1);
        }

        RegCloseKey(hk);
        return 1;
    }

    int RegDelete(lua_State* state) {
        const char* key_str = luaL_checkstring(state, 1);
        const bool has_name = !lua_isnoneornil(state, 2);
        std::wstring vname;
        if (has_name)
            vname = ::util::Utf8ToWide(luaL_checkstring(state, 2));

        HKEY root{};
        std::wstring subkey;
        if (!ParseRegKey(key_str, root, subkey)) {
            return luaL_error(state, "hw.sys.reg_delete: unknown hive in '%s'", key_str);
        }

        LONG rc = ERROR_SUCCESS;
        if (has_name) {
            HKEY hk{};
            if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS) {
                lua_pushboolean(state, 0);
                return 1;
            }
            rc = RegDeleteValueW(hk, vname.c_str());
            RegCloseKey(hk);
        } else {
            rc = RegDeleteKeyW(root, subkey.c_str());
        }

        lua_pushboolean(state, rc == ERROR_SUCCESS ? 1 : 0);
        return 1;
    }

} // namespace

void registerApi(lua_State* state) {
    util::setTableField(state, "sys", [](lua_State* s) {
        util::setFn(s, "lock", Lock);
        util::setFn(s, "env", Env);
        util::setFn(s, "username", Username);
        util::setFn(s, "debug_console", DebugConsole);
        util::setFn(s, "send_notify_message", SendNotifyMsg);
        util::setFn(s, "reg_get", RegGet);
        util::setFn(s, "reg_set", RegSet);
        util::setFn(s, "reg_exists", RegExists);
        util::setFn(s, "reg_delete", RegDelete);
    });
}

} // namespace lua::system
