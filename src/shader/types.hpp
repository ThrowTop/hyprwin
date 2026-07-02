#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace hw::shader {

inline std::filesystem::path NormalizePath(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        return path;
    }
    std::filesystem::path normalized = std::filesystem::weakly_canonical(absolute, ec);
    return ec ? absolute.lexically_normal() : normalized;
}

struct Bytecode {
    std::vector<std::byte> bytes;
};

struct CompileRequest {
    std::filesystem::path source_path;
    std::filesystem::path config_directory;
    std::uint32_t abi_version = 1;
    bool debug = false;
    std::uint64_t generation = 0;
};

struct CompileResult {
    bool success = false;
    Bytecode bytecode{};
    std::string diagnostics;
    std::filesystem::path source_path;
    std::uint64_t generation = 0;
};

} // namespace hw::shader
