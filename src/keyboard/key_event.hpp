#pragma once

#include <cstdint>

#include <windows.h>

namespace hw {

constexpr std::uint32_t kKeyDownFlag = 0x8000u;

inline constexpr std::uint32_t EncodeKey(UINT vk, WPARAM message) noexcept {
    return (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) ? (vk | kKeyDownFlag) : vk;
}

inline constexpr UINT DecodeKey(std::uint32_t encoded) noexcept {
    return encoded & ~kKeyDownFlag;
}

inline constexpr bool IsKeyDown(std::uint32_t encoded) noexcept {
    return (encoded & kKeyDownFlag) != 0;
}

inline constexpr bool IsKeyMessage(WPARAM message) noexcept {
    return message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP || message == WM_SYSKEYUP;
}

inline constexpr bool IsModifier(UINT vk) noexcept {
    switch (vk) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return true;
        default:
            return false;
    }
}

} // namespace hw
