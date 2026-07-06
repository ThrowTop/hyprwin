#pragma once

#include <string>

#include "overlay/outline/types.hpp"

namespace hw::outline::cache {

struct LookupResult {
    bool hit = false;
    Bytecode bytecode{};
    std::string diagnostics;
};

[[nodiscard]] LookupResult Load(const CompileRequest& request);
void Store(const CompileRequest& request, const Bytecode& bytecode);

} // namespace hw::outline::cache
