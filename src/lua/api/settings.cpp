#include "lua/api/internal.hpp"

#include "log/log.hpp"
#include "lua/binds/key_parse.hpp"
#include "lua/util/fields.hpp"
#include "lua/util/stack.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace lua::settings {
namespace {
    inline char kHwImplRegistryKey;
    inline char kSettingsRegistryKey;
    inline char kDebugSettingsRegistryKey;

    const char* ResizeCornerName(hw::ResizeCorner c) noexcept {
        switch (c) {
            case hw::ResizeCorner::Closest:
                return "closest";
            case hw::ResizeCorner::TopLeft:
                return "topleft";
            case hw::ResizeCorner::TopRight:
                return "topright";
            case hw::ResizeCorner::BottomLeft:
                return "bottomleft";
            case hw::ResizeCorner::BottomRight:
                return "bottomright";
        }
        return "closest";
    }

    const char* OverlayPreviewName(hw::OverlayPreview preview) noexcept {
        switch (preview) {
            case hw::OverlayPreview::Overlay:
                return "overlay";
            case hw::OverlayPreview::Live:
                return "live";
            case hw::OverlayPreview::Thumbnail:
                return "thumbnail";
        }
        return "overlay";
    }

    const auto kDebugFields = std::make_tuple(fields::auto_field("trace_binds", &hw::DebugSettings::trace_binds),
      fields::auto_field("bench_binds", &hw::DebugSettings::bench_binds),
      fields::auto_field("trace_grabs", &hw::DebugSettings::trace_grabs),
      fields::auto_field("trace_super", &hw::DebugSettings::trace_super),
      fields::auto_field("trace_timeout", &hw::DebugSettings::trace_timeout));

    void PushPalette(lua_State* state, const hw::ShaderPalette& palette) {
        lua_createtable(state, static_cast<int>(palette.count), 0);
        for (std::uint32_t i = 0; i < palette.count; ++i) {
            color::push(state, palette.colors[i]);
            lua_rawseti(state, -2, static_cast<int>(i + 1));
        }
    }

    std::optional<hw::ShaderPalette> ReadPalette(lua_State* state, int idx) {
        if (!lua_istable(state, idx)) {
            return std::nullopt;
        }

        const int table = util::absIndex(state, idx);
        std::array<hw::Color, hw::kMaxShaderColors> values{};
        std::uint32_t count = 0;

        lua_pushnil(state);
        while (lua_next(state, table) != 0) {
            if (lua_type(state, -2) != LUA_TNUMBER) {
                lua_pop(state, 2);
                return std::nullopt;
            }

            const lua_Number keyNumber = lua_tonumber(state, -2);
            const auto key = static_cast<std::uint32_t>(keyNumber);
            if (keyNumber != static_cast<lua_Number>(key) || key == 0 || key > hw::kMaxShaderColors) {
                lua_pop(state, 2);
                return std::nullopt;
            }

            hw::Color value{};
            if (!color::read(state, -1, value)) {
                lua_pop(state, 2);
                return std::nullopt;
            }

            values[key - 1] = value;
            count = std::max(count, key);
            lua_pop(state, 1);
        }

        if (count == 0 || count > hw::kMaxShaderColors) {
            return std::nullopt;
        }

        for (std::uint32_t i = 1; i <= count; ++i) {
            lua_rawgeti(state, table, static_cast<int>(i));
            const bool present = !lua_isnil(state, -1);
            lua_pop(state, 1);
            if (!present) {
                return std::nullopt;
            }
        }

        hw::ShaderPalette palette{};
        palette.colors = values;
        palette.count = count;
        return palette;
    }

    void PushShaderPath(lua_State* state, const std::wstring& path) {
        if (path.empty()) {
            lua_pushnil(state);
            return;
        }

        const std::string utf8 = ::util::WideToUtf8(path);
        lua_pushlstring(state, utf8.data(), utf8.size());
    }

    std::optional<std::wstring> ReadShaderPath(lua_State* state, int idx) {
        if (lua_isnil(state, idx)) {
            return std::wstring{};
        }
        if (!lua_isstring(state, idx)) {
            return std::nullopt;
        }

        const std::string text = util::toString(state, idx);
        if (text.find('\0') != std::string::npos) {
            return std::nullopt;
        }

        std::wstring path = ::util::Utf8ToWide(text);
        if (!text.empty() && path.empty()) {
            return std::nullopt;
        }
        return path;
    }

    std::optional<std::vector<hw::GrabFilterRule>> ReadGrabFilters(lua_State* state, int idx) {
        if (!lua_istable(state, idx)) {
            return std::nullopt;
        }

        const int table = util::absIndex(state, idx);
        const std::size_t count = lua_objlen(state, table);
        std::vector<hw::GrabFilterRule> rules;
        rules.reserve(count);

        for (std::size_t i = 1; i <= count; ++i) {
            lua_rawgeti(state, table, static_cast<int>(i));
            if (!lua_istable(state, -1)) {
                lua_pop(state, 1);
                return std::nullopt;
            }

            const int ruleTable = util::absIndex(state, -1);
            hw::GrabFilterRule rule{};

            lua_getfield(state, ruleTable, "action");
            if (!lua_isstring(state, -1)) {
                lua_pop(state, 2);
                return std::nullopt;
            }
            const std::string action = util::toString(state, -1);
            lua_pop(state, 1);
            if (action == "include") {
                rule.action = hw::GrabFilterAction::Include;
            } else if (action == "exclude") {
                rule.action = hw::GrabFilterAction::Exclude;
            } else {
                lua_pop(state, 1);
                return std::nullopt;
            }

            const auto readStringField = [&](const char* field, std::wstring& output) {
                lua_getfield(state, ruleTable, field);
                if (lua_isnil(state, -1)) {
                    lua_pop(state, 1);
                    return true;
                }
                if (!lua_isstring(state, -1)) {
                    lua_pop(state, 1);
                    return false;
                }
                const std::string value = util::toString(state, -1);
                lua_pop(state, 1);
                if (value.empty() || value.find('\0') != std::string::npos) {
                    return false;
                }
                output = ::util::Utf8ToWide(value);
                return !output.empty();
            };

            if (!readStringField("process", rule.process) || !readStringField("class", rule.window_class) || (rule.process.empty() && rule.window_class.empty())) {
                lua_pop(state, 1);
                return std::nullopt;
            }

            lua_pushnil(state);
            while (lua_next(state, ruleTable) != 0) {
                if (!lua_isstring(state, -2)) {
                    lua_pop(state, 3);
                    return std::nullopt;
                }
                const std::string field = util::toString(state, -2);
                lua_pop(state, 1);
                if (field != "action" && field != "process" && field != "class") {
                    lua_pop(state, 2);
                    return std::nullopt;
                }
            }

            rules.push_back(std::move(rule));
            lua_pop(state, 1);
        }

        lua_pushnil(state);
        while (lua_next(state, table) != 0) {
            if (!lua_isnumber(state, -2)) {
                lua_pop(state, 2);
                return std::nullopt;
            }
            const lua_Number keyNumber = lua_tonumber(state, -2);
            const auto key = static_cast<std::size_t>(keyNumber);
            lua_pop(state, 1);
            if (keyNumber != static_cast<lua_Number>(key) || key == 0 || key > count) {
                lua_pop(state, 1);
                return std::nullopt;
            }
        }

        return rules;
    }

    void PushGrabFilters(lua_State* state, const std::vector<hw::GrabFilterRule>& rules) {
        lua_createtable(state, static_cast<int>(rules.size()), 0);
        for (std::size_t i = 0; i < rules.size(); ++i) {
            const hw::GrabFilterRule& rule = rules[i];
            lua_createtable(state, 0, 3);
            lua_pushstring(state, rule.action == hw::GrabFilterAction::Include ? "include" : "exclude");
            lua_setfield(state, -2, "action");
            if (!rule.process.empty()) {
                const std::string process = ::util::WideToUtf8(rule.process);
                lua_pushlstring(state, process.data(), process.size());
                lua_setfield(state, -2, "process");
            }
            if (!rule.window_class.empty()) {
                const std::string windowClass = ::util::WideToUtf8(rule.window_class);
                lua_pushlstring(state, windowClass.data(), windowClass.size());
                lua_setfield(state, -2, "class");
            }
            lua_rawseti(state, -2, static_cast<int>(i + 1));
        }
    }

    const auto kSettingsFields = std::make_tuple(fields::auto_field("border", &hw::Settings::border),
      fields::auto_field("gradient_angle", &hw::Settings::gradient_angle),
      fields::auto_field("rotating", &hw::Settings::rotating),
      fields::auto_field("rotation_speed", &hw::Settings::rotation_speed),
      fields::auto_field("corner_radius", &hw::Settings::corner_radius),
      fields::auto_field("outer_alpha", &hw::Settings::outer_alpha),
      fields::auto_field("glow_falloff", &hw::Settings::glow_falloff),
      fields::custom_field("colors", &hw::Settings::shader_palette, PushPalette, ReadPalette),
      fields::custom_field(
        "move_preview",
        &hw::Settings::move_preview,
        [](lua_State* s, hw::OverlayPreview v) { lua_pushstring(s, OverlayPreviewName(v)); },
        [](lua_State* s, int idx) -> std::optional<hw::OverlayPreview> {
            if (!lua_isstring(s, idx))
                return std::nullopt;
            const std::string str = util::toString(s, idx);
            if (str == "overlay")
                return hw::OverlayPreview::Overlay;
            if (str == "live")
                return hw::OverlayPreview::Live;
            if (str == "thumbnail")
                return hw::OverlayPreview::Thumbnail;
            return std::nullopt;
        }),
      fields::custom_field(
        "resize_preview",
        &hw::Settings::resize_preview,
        [](lua_State* s, hw::OverlayPreview v) { lua_pushstring(s, OverlayPreviewName(v)); },
        [](lua_State* s, int idx) -> std::optional<hw::OverlayPreview> {
            if (!lua_isstring(s, idx))
                return std::nullopt;
            const std::string str = util::toString(s, idx);
            if (str == "overlay")
                return hw::OverlayPreview::Overlay;
            if (str == "live")
                return hw::OverlayPreview::Live;
            if (str == "thumbnail")
                return hw::OverlayPreview::Thumbnail;
            return std::nullopt;
        }),
      fields::custom_field(
        "live_preview_rate",
        &hw::Settings::live_preview_rate,
        [](lua_State* s, std::uint32_t v) { lua_pushinteger(s, static_cast<lua_Integer>(v)); },
        [](lua_State* s, int idx) -> std::optional<std::uint32_t> {
            if (!lua_isnumber(s, idx))
                return std::nullopt;
            const lua_Number number = lua_tonumber(s, idx);
            if (!std::isfinite(number))
                return std::nullopt;
            if (number <= 0)
                return 0;
            if (number >= static_cast<lua_Number>(std::numeric_limits<std::uint32_t>::max()))
                return std::numeric_limits<std::uint32_t>::max();
            return static_cast<std::uint32_t>(number);
        }),
      fields::custom_field(
        "resize_corner",
        &hw::Settings::resize_corner,
        [](lua_State* s, hw::ResizeCorner v) { lua_pushstring(s, ResizeCornerName(v)); },
        [](lua_State* s, int idx) -> std::optional<hw::ResizeCorner> {
            if (!lua_isstring(s, idx))
                return std::nullopt;
            const std::string str = util::toString(s, idx);
            if (str == "closest")
                return hw::ResizeCorner::Closest;
            if (str == "topleft")
                return hw::ResizeCorner::TopLeft;
            if (str == "topright")
                return hw::ResizeCorner::TopRight;
            if (str == "bottomleft")
                return hw::ResizeCorner::BottomLeft;
            if (str == "bottomright")
                return hw::ResizeCorner::BottomRight;
            return std::nullopt;
        }));

    void PushRegistryValue(lua_State* state, void* key) {
        lua_pushlightuserdata(state, key);
        lua_rawget(state, LUA_REGISTRYINDEX);
    }

    void MaybePublish(Context& context) {
        if (context.loading_config && *context.loading_config)
            return;
        if (context.publish_settings)
            context.publish_settings();
        if (context.notify_settings_changed)
            context.notify_settings_changed();
    }

    bool ApplySettingFromLua(lua_State* state, Context& context, std::string_view key, int idx) {
        hw::Settings& s = *context.pending_settings;

        if (key == "super") {
            if (!lua_isstring(state, idx)) {
                LOG_ERROR("lua: rejected setting 'super'");
                return false;
            }
            const std::string val = util::toString(state, idx);
            const UINT vk = ParseSuperKey(val);
            if (vk != VK_LWIN && vk != VK_RWIN) {
                LOG_WARN("lua: invalid super key '{}'; using LWIN", val);
                s.super_vk = VK_LWIN;
            } else {
                s.super_vk = vk;
            }
            MaybePublish(context);
            return true;
        }

        if (key == "debug") {
            if (!lua_istable(state, idx)) {
                LOG_ERROR("lua: hw.settings.debug must be a table");
                return false;
            }
            lua_pushnil(state);
            while (lua_next(state, idx) != 0) {
                if (lua_isstring(state, -2)) {
                    const std::string field = util::toString(state, -2);
                    if (!fields::applyField(state, s.debug, field, lua_gettop(state), kDebugFields)) {
                        LOG_ERROR("lua: unknown hw.settings.debug field '{}'", field);
                    }
                }
                lua_pop(state, 1);
            }
            MaybePublish(context);
            return true;
        }

        if (key == "shader") {
            auto shaderPath = ReadShaderPath(state, idx);
            if (!shaderPath) {
                LOG_ERROR("lua: rejected setting 'shader'");
                return false;
            }

            if (!shaderPath->empty()) {
                std::filesystem::path path{*shaderPath};
                if (path.is_relative()) {
                    path = std::filesystem::path{::util::Utf8ToWide(context.config_path)}.parent_path() / path;
                }
                std::error_code ec;
                path = std::filesystem::weakly_canonical(path, ec);
                if (ec) {
                    ec.clear();
                    path = std::filesystem::absolute(path, ec).lexically_normal();
                    if (ec) {
                        LOG_ERROR("lua: rejected setting 'shader'");
                        return false;
                    }
                }
                *shaderPath = path.native();
            }

            s.shader_path = std::move(*shaderPath);
            MaybePublish(context);
            return true;
        }

        if (key == "grab_filters") {
            auto rules = ReadGrabFilters(state, idx);
            if (!rules) {
                LOG_ERROR("lua: rejected setting 'grab_filters'");
                return false;
            }
            s.grab_filters = std::move(*rules);
            MaybePublish(context);
            return true;
        }

        if (!fields::applyField(state, s, key, idx, kSettingsFields)) {
            LOG_ERROR("lua: rejected setting '{}'", key);
            return false;
        }
        MaybePublish(context);
        return true;
    }

    int DebugSettingsIndex(lua_State* state) {
        Context* context = lua::context(state);
        if (!context || !context->pending_settings)
            util::raise(state, "lua api context missing");
        const std::string key = util::toString(state, 2);
        if (key == "last_config_load_ms") {
            lua_pushinteger(state, context->pending_settings->debug.last_config_load_ms);
            return 1;
        }
        fields::pushField(state, context->pending_settings->debug, key, kDebugFields);
        return 1;
    }

    int DebugSettingsNewIndex(lua_State* state) {
        Context* context = lua::context(state);
        if (!context || !context->pending_settings)
            util::raise(state, "lua api context missing");
        const std::string key = util::toString(state, 2);
        if (!fields::applyField(state, context->pending_settings->debug, key, 3, kDebugFields)) {
            util::raise(state, "unknown hw.settings.debug field");
        }
        return 0;
    }

    int SettingsIndex(lua_State* state) {
        Context* context = lua::context(state);
        if (!context || !context->pending_settings)
            util::raise(state, "lua api context missing");
        const hw::Settings& s = *context->pending_settings;
        const std::string key = util::toString(state, 2);
        if (key == "super") {
            lua_pushstring(state, s.super_vk == VK_RWIN ? "RWIN" : "LWIN");
        } else if (key == "debug") {
            PushRegistryValue(state, &kDebugSettingsRegistryKey);
        } else if (key == "shader") {
            PushShaderPath(state, s.shader_path);
        } else if (key == "grab_filters") {
            PushGrabFilters(state, s.grab_filters);
        } else {
            fields::pushField(state, s, key, kSettingsFields);
        }
        return 1;
    }

    int SettingsNewIndex(lua_State* state) {
        Context* context = lua::context(state);
        if (!context || !context->pending_settings)
            util::raise(state, "lua api context missing");
        ApplySettingFromLua(state, *context, util::toString(state, 2), 3);
        return 0;
    }
} // namespace

void registerApi(lua_State* state) {
    lua_newtable(state);
    lua_newtable(state);
    util::setFn(state, "__index", DebugSettingsIndex);
    util::setFn(state, "__newindex", DebugSettingsNewIndex);
    lua_setmetatable(state, -2);
    lua_pushlightuserdata(state, &kDebugSettingsRegistryKey);
    lua_pushvalue(state, -2);
    lua_rawset(state, LUA_REGISTRYINDEX);
    lua_pop(state, 1);

    lua_newtable(state);
    lua_newtable(state);
    util::setFn(state, "__index", SettingsIndex);
    util::setFn(state, "__newindex", SettingsNewIndex);
    lua_setmetatable(state, -2);
    lua_pushlightuserdata(state, &kSettingsRegistryKey);
    lua_pushvalue(state, -2);
    lua_rawset(state, LUA_REGISTRYINDEX);
    lua_pop(state, 1);
}

int hwIndex(lua_State* state) {
    const std::string key = util::toString(state, 2);
    if (key == "settings") {
        PushRegistryValue(state, &kSettingsRegistryKey);
        return 1;
    }
    PushRegistryValue(state, &kHwImplRegistryKey);
    lua_pushvalue(state, 2);
    lua_rawget(state, -2);
    return 1;
}

int hwNewIndex(lua_State* state) {
    Context* context = lua::context(state);
    if (!context || !context->pending_settings)
        util::raise(state, "lua api context missing");

    const std::string key = util::toString(state, 2);
    if (key != "settings")
        util::raise(state, "unknown hw assignment");
    if (!lua_istable(state, 3))
        util::raise(state, "hw.settings must be assigned a table");

    lua_pushnil(state);
    while (lua_next(state, 3) != 0) {
        if (!lua_isstring(state, -2)) {
            lua_pop(state, 1);
            util::raise(state, "hw.settings keys must be strings");
        }
        ApplySettingFromLua(state, *context, util::toString(state, -2), lua_gettop(state));
        lua_pop(state, 1);
    }
    return 0;
}

void storeHwImpl(lua_State* state) {
    lua_pushlightuserdata(state, &kHwImplRegistryKey);
    lua_pushvalue(state, -2);
    lua_rawset(state, LUA_REGISTRYINDEX);
}
} // namespace lua::settings
