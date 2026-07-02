#pragma once

#include <string>

namespace lua {

struct SafeMode {
    bool active = true;
    std::string reason;
};

} // namespace lua
