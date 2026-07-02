#include "lua/api/internal.hpp"

#include "lua/util/stack.hpp"
#include "util/strings.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

namespace lua::fs {
namespace {

    std::filesystem::path ConfigDir(lua_State* state) {
        Context* context = lua::context(state);
        if (!context || context->config_path.empty())
            return {};
        return std::filesystem::path{::util::Utf8ToWide(context->config_path)}.parent_path();
    }

    std::filesystem::path ResolvePath(lua_State* state, int index) {
        std::size_t len = 0;
        const char* raw = luaL_checklstring(state, index, &len);
        const std::string_view text{raw, len};
        if (text.find('\0') != std::string_view::npos)
            luaL_argerror(state, index, "path must not contain embedded null bytes");

        std::wstring wide = ::util::Utf8ToWide(text);
        if (!text.empty() && wide.empty())
            luaL_argerror(state, index, "path must be valid UTF-8");

        std::filesystem::path path{std::move(wide)};
        if (path.is_relative()) {
            const std::filesystem::path base = ConfigDir(state);
            if (!base.empty())
                path = base / path;
        }

        return path.lexically_normal();
    }

    void PushString(lua_State* state, std::wstring_view value) {
        const std::string utf8 = ::util::WideToUtf8(value);
        lua_pushlstring(state, utf8.data(), utf8.size());
    }

    void SetStringField(lua_State* state, const char* field, const std::filesystem::path& path) {
        PushString(state, path.native());
        lua_setfield(state, -2, field);
    }

    int PushNilString(lua_State* state, std::string_view message) {
        lua_pushnil(state);
        lua_pushlstring(state, message.data(), message.size());
        return 2;
    }

    int PushFalseString(lua_State* state, std::string_view message) {
        lua_pushboolean(state, 0);
        lua_pushlstring(state, message.data(), message.size());
        return 2;
    }

    int PushNilError(lua_State* state, const std::error_code& ec) {
        return PushNilString(state, ec.message());
    }

    int PushFalseError(lua_State* state, const std::error_code& ec) {
        return PushFalseString(state, ec.message());
    }

    int Exists(lua_State* state) {
        const auto path = ResolvePath(state, 1);
        std::error_code ec;
        const bool ok = std::filesystem::exists(path, ec);
        lua_pushboolean(state, !ec && ok);
        return 1;
    }

    int IsFile(lua_State* state) {
        const auto path = ResolvePath(state, 1);
        std::error_code ec;
        const bool ok = std::filesystem::is_regular_file(path, ec);
        lua_pushboolean(state, !ec && ok);
        return 1;
    }

    int IsDir(lua_State* state) {
        const auto path = ResolvePath(state, 1);
        std::error_code ec;
        const bool ok = std::filesystem::is_directory(path, ec);
        lua_pushboolean(state, !ec && ok);
        return 1;
    }

    int List(lua_State* state) {
        const auto path = ResolvePath(state, 1);
        std::error_code ec;
        std::filesystem::directory_iterator it(path, ec);
        if (ec)
            return PushNilError(state, ec);

        lua_newtable(state);
        int index = 1;
        for (; it != std::filesystem::directory_iterator{}; it.increment(ec)) {
            if (ec)
                break;

            const auto entry_path = it->path().lexically_normal();
            lua_newtable(state);

            SetStringField(state, "name", entry_path.filename());
            SetStringField(state, "path", entry_path);
            SetStringField(state, "ext", entry_path.extension());

            std::error_code status_ec;
            const auto status = it->symlink_status(status_ec);
            const char* type = "other";
            if (!status_ec) {
                if (std::filesystem::is_regular_file(status))
                    type = "file";
                else if (std::filesystem::is_directory(status))
                    type = "directory";
                else if (std::filesystem::is_symlink(status))
                    type = "symlink";
            }
            lua_pushstring(state, type);
            lua_setfield(state, -2, "type");

            if (!status_ec && std::filesystem::is_regular_file(status)) {
                std::error_code size_ec;
                const auto size = it->file_size(size_ec);
                if (!size_ec && size <= static_cast<std::uintmax_t>(std::numeric_limits<lua_Integer>::max())) {
                    lua_pushinteger(state, static_cast<lua_Integer>(size));
                    lua_setfield(state, -2, "size");
                }
            }

            lua_rawseti(state, -2, index++);
        }

        if (ec) {
            lua_pop(state, 1);
            return PushNilError(state, ec);
        }

        return 1;
    }

    int Read(lua_State* state) {
        const auto path = ResolvePath(state, 1);
        std::error_code ec;
        const bool is_file = std::filesystem::is_regular_file(path, ec);
        if (ec)
            return PushNilError(state, ec);
        if (!is_file)
            return PushNilString(state, "not a file");

        std::ifstream file(path, std::ios::binary);
        if (!file)
            return PushNilString(state, "failed to open file");

        std::string data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (!file.good() && !file.eof())
            return PushNilString(state, "failed to read file");

        lua_pushlstring(state, data.data(), data.size());
        return 1;
    }

    int Write(lua_State* state) {
        const auto path = ResolvePath(state, 1);
        std::size_t len = 0;
        const char* text = luaL_checklstring(state, 2, &len);
        if (len > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
            return PushFalseString(state, "file too large");

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return PushFalseString(state, "failed to open file");

        file.write(text, static_cast<std::streamsize>(len));
        if (!file)
            return PushFalseString(state, "failed to write file");

        lua_pushboolean(state, 1);
        return 1;
    }

    int Mkdir(lua_State* state) {
        const auto path = ResolvePath(state, 1);
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec)
            return PushFalseError(state, ec);

        const bool is_dir = std::filesystem::is_directory(path, ec);
        if (ec)
            return PushFalseError(state, ec);
        if (!is_dir)
            return PushFalseString(state, "not a directory");

        lua_pushboolean(state, 1);
        return 1;
    }

} // namespace

void registerApi(lua_State* state) {
    lua_newtable(state);
    util::setFn(state, "exists", Exists);
    util::setFn(state, "is_file", IsFile);
    util::setFn(state, "is_dir", IsDir);
    util::setFn(state, "list", List);
    util::setFn(state, "read", Read);
    util::setFn(state, "write", Write);
    util::setFn(state, "mkdir", Mkdir);
    lua_setfield(state, -2, "fs");
}

} // namespace lua::fs
