#pragma once

#include <string>

#include "shader/types.hpp"

namespace hw::shader {

struct CompilerAvailability {
    bool available = false;
    std::string diagnostics;
};

class Compiler {
  public:
    static CompilerAvailability CheckAvailability();

    CompileResult Compile(const CompileRequest& request);
};

} // namespace hw::shader
