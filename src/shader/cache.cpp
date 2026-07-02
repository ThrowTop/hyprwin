#include "shader/cache.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <windows.h>

#include "util/strings.hpp"
#include "win/native.hpp"

namespace hw::shader::cache {
namespace {

    constexpr std::uint32_t kCacheVersion = 2;
    constexpr std::size_t kMaxBytecodeSize = 16 * 1024 * 1024;

    std::optional<std::filesystem::path> AppLocalCacheDirectory() {
        const std::filesystem::path moduleDirectory = win::GetModuleDirectory();
        if (moduleDirectory.empty()) {
            return std::nullopt;
        }

        std::filesystem::path directory = moduleDirectory / L"shader-cache";
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        return ec ? std::nullopt : std::optional{directory};
    }

    std::uint64_t PathHash(std::wstring_view path) noexcept {
        std::uint64_t hash = 14695981039346656037ull;
        for (wchar_t ch : path) {
            hash ^= static_cast<std::uint64_t>(ch);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::string Hex(std::uint64_t value) {
        constexpr char digits[] = "0123456789abcdef";
        std::string out(16, '0');
        for (std::size_t i = 0; i < out.size(); ++i) {
            const unsigned shift = static_cast<unsigned>((out.size() - 1 - i) * 4);
            out[i] = digits[(value >> shift) & 0xF];
        }
        return out;
    }

    std::string CacheStem(const CompileRequest& request) {
        const std::filesystem::path source = NormalizePath(request.source_path);
        std::string stem = ::util::WideToUtf8(source.stem().native());
        if (stem.empty()) {
            stem = "shader";
        }
        for (char& ch : stem) {
            const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
            if (!ok) {
                ch = '_';
            }
        }
        return stem + "_" + Hex(PathHash(source.native()));
    }

    std::optional<std::filesystem::path> BytecodePath(const CompileRequest& request) {
        std::optional<std::filesystem::path> directory = AppLocalCacheDirectory();
        if (!directory) {
            return std::nullopt;
        }
        return *directory / (::util::Utf8ToWide(CacheStem(request)) + L".dxbc");
    }

    std::optional<std::filesystem::path> MetaPath(const CompileRequest& request) {
        std::optional<std::filesystem::path> bytecode = BytecodePath(request);
        if (!bytecode) {
            return std::nullopt;
        }
        return std::filesystem::path(bytecode->native() + L".meta");
    }

    std::optional<std::uint64_t> FileTimeTicks(const std::filesystem::path& path) {
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(path, ec);
        if (ec) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(time.time_since_epoch().count());
    }

    std::string ExpectedMeta(const CompileRequest& request) {
        std::ostringstream out;
        out << "version=" << kCacheVersion << '\n';
        out << "abi=" << request.abi_version << '\n';
        out << "debug=" << (request.debug ? 1 : 0) << '\n';
        out << "profile=ps_5_0\n";
        out << "source=" << ::util::WideToUtf8(NormalizePath(request.source_path).native()) << '\n';
        out << "mtime=" << FileTimeTicks(request.source_path).value_or(0) << '\n';
        return out.str();
    }

    std::optional<std::string> ReadText(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::nullopt;
        }
        std::ostringstream out;
        out << file.rdbuf();
        return out.str();
    }

    std::optional<std::vector<std::byte>> ReadBytes(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            return std::nullopt;
        }

        const std::streamoff size = file.tellg();
        if (size <= 0 || static_cast<std::uint64_t>(size) > kMaxBytecodeSize) {
            return std::nullopt;
        }

        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        return file.good() ? std::optional{std::move(bytes)} : std::nullopt;
    }

    bool WriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return file.good();
    }

    bool WriteText(const std::filesystem::path& path, std::string_view text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return file.good();
    }

} // namespace

LookupResult Load(const CompileRequest& request) {
    const std::optional<std::filesystem::path> bytecodePath = BytecodePath(request);
    const std::optional<std::filesystem::path> metaPath = MetaPath(request);
    if (!bytecodePath || !metaPath) {
        return LookupResult{.diagnostics = "shader cache directory unavailable"};
    }

    const std::optional<std::string> meta = ReadText(*metaPath);
    if (!meta || *meta != ExpectedMeta(request)) {
        return LookupResult{.diagnostics = "shader cache miss"};
    }

    std::optional<std::vector<std::byte>> bytes = ReadBytes(*bytecodePath);
    if (!bytes) {
        return LookupResult{.diagnostics = "shader cache miss"};
    }

    return LookupResult{
      .hit = true,
      .bytecode = Bytecode{std::move(*bytes)},
      .diagnostics = "shader cache hit",
    };
}

void Store(const CompileRequest& request, const Bytecode& bytecode) {
    if (bytecode.bytes.empty()) {
        return;
    }

    const std::optional<std::filesystem::path> bytecodePath = BytecodePath(request);
    const std::optional<std::filesystem::path> metaPath = MetaPath(request);
    if (!bytecodePath || !metaPath) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(bytecodePath->parent_path(), ec);
    if (ec) {
        return;
    }

    const std::filesystem::path tempBytecode(bytecodePath->native() + L".tmp");
    const std::filesystem::path tempMeta(metaPath->native() + L".tmp");
    if (!WriteBytes(tempBytecode, bytecode.bytes) || !WriteText(tempMeta, ExpectedMeta(request))) {
        std::filesystem::remove(tempBytecode, ec);
        std::filesystem::remove(tempMeta, ec);
        return;
    }

    std::filesystem::rename(tempBytecode, *bytecodePath, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(*bytecodePath, ec);
        ec.clear();
        std::filesystem::rename(tempBytecode, *bytecodePath, ec);
    }
    ec.clear();
    std::filesystem::rename(tempMeta, *metaPath, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(*metaPath, ec);
        ec.clear();
        std::filesystem::rename(tempMeta, *metaPath, ec);
    }
}

} // namespace hw::shader::cache
