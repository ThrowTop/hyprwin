#pragma once

#include <windows.h>

#include "win/window.hpp"

namespace win::detail {

[[nodiscard]] bool IsAppWindow(HWND hwnd, WindowFilterReason* rejection = nullptr) noexcept;

} // namespace win::detail
