#pragma once

#include <cstdint>

#include <windows.h>

namespace lua {

enum Modifier : std::uint8_t {
    ModLShift = 1u << 0,
    ModRShift = 1u << 1,
    ModLCtrl = 1u << 2,
    ModRCtrl = 1u << 3,
    ModLAlt = 1u << 4,
    ModRAlt = 1u << 5,
};

struct KeyEvent {
    UINT vk = 0;
    std::uint8_t modifiers = 0;

    bool operator==(const KeyEvent&) const = default;
};

struct KeyEventHash {
    std::size_t operator()(const KeyEvent& event) const noexcept {
        return (static_cast<std::size_t>(event.vk) << 8) ^ event.modifiers;
    }
};

} // namespace lua
