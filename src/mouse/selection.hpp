#pragma once

#include "config/settings.hpp"
#include "win/native.hpp"

namespace hw::mouse {
[[nodiscard]] win::WindowAtPointResult SelectTarget(POINT pt, const Settings& settings) noexcept;
}
