#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "lua/binds/key_event.hpp"

namespace lua {

[[nodiscard]] bool ParseKeyBinding(std::string_view text, std::vector<KeyEvent>& out, std::string& error);
[[nodiscard]] UINT ParseSuperKey(std::string_view text) noexcept;
[[nodiscard]] std::string FormatKeyEvent(const KeyEvent& event);

} // namespace lua
