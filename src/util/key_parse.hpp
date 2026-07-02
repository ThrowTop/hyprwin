#pragma once

#include <string_view>
#include <windows.h>

namespace util {

// Maps a key name token to a virtual key code.
// Case-insensitive. Returns 0 if unrecognized.
// Examples: "A" -> 'A', "F1" -> VK_F1, "LEFT" -> VK_LEFT, "PAUSE" -> VK_PAUSE
[[nodiscard]] UINT ParseVirtualKey(std::string_view text) noexcept;

} // namespace util
