#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
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

struct DebugSettings {
    bool trace_binds = false;
    bool bench_binds = false;
    bool trace_grabs = false;
    bool trace_super = false;
    bool trace_timeout = false;
    std::uint32_t last_config_load_ms = 0;
    bool operator==(const DebugSettings&) const = default;
};

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

    ResizeCorner resize_corner = ResizeCorner::Closest;
    std::vector<GrabFilterRule> grab_filters;
    DebugSettings debug{};

    bool operator==(const Settings&) const = default;
};

using SettingsPtr = std::shared_ptr<const Settings>;
using AtomicSettingsPtr = std::atomic<SettingsPtr>;

inline SettingsPtr DefaultSettings() {
    return std::make_shared<Settings>();
}

inline SettingsPtr LoadSettingsSnapshot(const AtomicSettingsPtr* settings, SettingsPtr fallback) noexcept {
    if (!settings) {
        return fallback;
    }

    SettingsPtr snapshot = settings->load(std::memory_order_acquire);
    return snapshot ? snapshot : fallback;
}
} // namespace hw
