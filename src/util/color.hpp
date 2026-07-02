#pragma once

#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace hw {

struct Color {
    std::uint8_t r = 0, g = 162, b = 255, a = 255;

    bool operator==(const Color&) const = default;

    [[nodiscard]] static constexpr std::uint8_t ToByte(float value) noexcept {
        if (value <= 0.0f) {
            return 0;
        }
        if (value >= 1.0f) {
            return 255;
        }
        return static_cast<std::uint8_t>(value * 255.0f + 0.5f);
    }

    [[nodiscard]] static constexpr float ToFloat(std::uint8_t value) noexcept {
        return static_cast<float>(value) / 255.0f;
    }

    [[nodiscard]] static constexpr Color FromBytes(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) noexcept {
        return Color{r, g, b, a};
    }

    [[nodiscard]] static constexpr Color FromFloats(float r, float g, float b, float a = 1.0f) noexcept {
        return Color{ToByte(r), ToByte(g), ToByte(b), ToByte(a)};
    }

    [[nodiscard]] static bool FromHexRgb(std::string_view text, Color& color) noexcept {
        if (text.size() != 6) {
            return false;
        }

        unsigned value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
            return false;
        }

        color = Color{
          static_cast<std::uint8_t>((value >> 16) & 0xffu),
          static_cast<std::uint8_t>((value >> 8) & 0xffu),
          static_cast<std::uint8_t>(value & 0xffu),
        };
        return true;
    }

    [[nodiscard]] std::string ToHexRgb() const {
        return std::format("{:02x}{:02x}{:02x}", r, g, b);
    }

    [[nodiscard]] Color WithAlpha(float alpha) const noexcept {
        return Color{r, g, b, ToByte(alpha)};
    }

    [[nodiscard]] Color WithAlphaByte(std::uint8_t alpha) const noexcept {
        return Color{r, g, b, alpha};
    }

    [[nodiscard]] Color WithAlphaMultiplier(float multiplier) const noexcept {
        return WithAlpha(ToFloat(a) * multiplier);
    }
};

static_assert(sizeof(Color) == 4, "hw::Color must be 4 bytes to match hw_color_t FFI layout");

} // namespace hw
