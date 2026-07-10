#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/color.hpp"

#include <windows.h>

namespace hw {
inline constexpr std::size_t kMaxShaderColors = 16;
inline constexpr std::uint32_t kShaderAbiVersion = 2;

struct ShaderPalette {
    std::array<Color, kMaxShaderColors> colors{
      Color{0, 162, 255, 255},
      Color{255, 0, 247, 255},
    };
    std::uint32_t count = 2;

    bool operator==(const ShaderPalette&) const = default;
};

enum class ResizeCorner {
    Closest,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

enum class OverlayPreview {
    Overlay,
    Live,
    Thumbnail,
};

enum class GrabFilterAction {
    Include,
    Exclude,
};

struct GrabFilterRule {
    GrabFilterAction action = GrabFilterAction::Exclude;
    std::wstring process;
    std::wstring window_class;

    bool operator==(const GrabFilterRule&) const = default;
};

// Keep the C++ identifier and Lua name for every debug toggle in one place.
#define HW_DEBUG_FLAG_LIST(X)                        \
    X(TraceBinds, "trace_binds")                    \
    X(BenchBinds, "bench_binds")                    \
    X(TraceGrabs, "trace_grabs")                    \
    X(TraceSuper, "trace_super")                    \
    X(TraceTimeout, "trace_timeout")                \
    X(Overlay, "overlay")                           \
    X(WindowPlacement, "window_placement")          \
    X(Interaction, "interaction")                   \
    X(Snapshot, "snapshot")

enum class DebugFlag : std::uint8_t {
#define HW_DEBUG_FLAG_ENUM(name, luaName) name,
    HW_DEBUG_FLAG_LIST(HW_DEBUG_FLAG_ENUM)
#undef HW_DEBUG_FLAG_ENUM
    Count,
};

inline constexpr std::array<std::pair<std::string_view, DebugFlag>, std::to_underlying(DebugFlag::Count)> kDebugFlags{{
#define HW_DEBUG_FLAG_ENTRY(name, luaName) {luaName, DebugFlag::name},
  HW_DEBUG_FLAG_LIST(HW_DEBUG_FLAG_ENTRY)
#undef HW_DEBUG_FLAG_ENTRY
}};

#undef HW_DEBUG_FLAG_LIST

[[nodiscard]] constexpr std::optional<DebugFlag> ParseDebugFlag(std::string_view name) noexcept {
    for (const auto& [candidate, flag] : kDebugFlags) {
        if (candidate == name) {
            return flag;
        }
    }
    return std::nullopt;
}

struct DebugSettings {
    using Mask = std::uint64_t;

    Mask flags = 0;

    [[nodiscard]] constexpr bool enabled(DebugFlag flag) const noexcept {
        return (flags & (Mask{1} << std::to_underlying(flag))) != 0;
    }

    constexpr void set(DebugFlag flag, bool value = true) noexcept {
        const Mask bit = Mask{1} << std::to_underlying(flag);
        if (value) {
            flags |= bit;
        } else {
            flags &= ~bit;
        }
    }

    bool operator==(const DebugSettings&) const = default;
};

static_assert(std::to_underlying(DebugFlag::Count) <= 64);

struct Settings {
    UINT super_vk = VK_LWIN;

    float border = 3.0f;
    ShaderPalette shader_palette{};
    std::wstring shader_path{};
    float gradient_angle = 45.0f;
    bool rotating = true;
    float rotation_speed = 120.0f;

    float corner_radius = 8.0f;
    float outer_alpha = 0.5f;
    float glow_falloff = 0.15f;

    OverlayPreview move_preview = OverlayPreview::Overlay;
    OverlayPreview resize_preview = OverlayPreview::Overlay;
    std::uint32_t live_preview_rate = 60;
    ResizeCorner resize_corner = ResizeCorner::Closest;
    std::vector<GrabFilterRule> grab_filters;
    DebugSettings debug{};

    bool operator==(const Settings&) const = default;
};

using SettingsPtr = std::shared_ptr<const Settings>;
using AtomicSettingsPtr = std::atomic<SettingsPtr>;

inline SettingsPtr DefaultSettings() {
    static const SettingsPtr settings = std::make_shared<const Settings>();
    return settings;
}

inline SettingsPtr LoadSettingsSnapshot(const AtomicSettingsPtr* settings) noexcept {
    if (settings) {
        SettingsPtr snapshot = settings->load(std::memory_order_acquire);
        if (snapshot) {
            return snapshot;
        }
    }
    return DefaultSettings();
}

inline SettingsPtr LoadSettingsSnapshot(const AtomicSettingsPtr* settings, SettingsPtr fallback) noexcept {
    if (!settings) {
        return fallback;
    }

    SettingsPtr snapshot = settings->load(std::memory_order_acquire);
    return snapshot ? snapshot : fallback;
}
} // namespace hw
