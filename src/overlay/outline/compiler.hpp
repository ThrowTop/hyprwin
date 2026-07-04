#pragma once

#include <string>

#include "overlay/outline/types.hpp"

namespace hw::outline {

struct CompilerAvailability {
    bool available = false;
    std::string diagnostics;
};

class Compiler {
  public:
    static CompilerAvailability CheckAvailability();

    CompileResult Compile(const CompileRequest& request);
};

} // namespace hw::outline
