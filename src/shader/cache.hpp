#pragma once

#include <string>

#include "shader/types.hpp"

namespace hw::shader::cache {

struct LookupResult {
    bool hit = false;
    Bytecode bytecode{};
    std::string diagnostics;
};

[[nodiscard]] LookupResult Load(const CompileRequest& request);
void Store(const CompileRequest& request, const Bytecode& bytecode);

} // namespace hw::shader::cache
