#include "overlay/outline/compiler.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <d3dcompiler.h>
#include <windows.h>
#include <wrl/client.h>

#include "util/strings.hpp"

namespace hw::outline {
namespace {
    using Microsoft::WRL::ComPtr;

    using D3DCompileFn = decltype(&D3DCompile);

    struct Module {
        HMODULE handle = nullptr;

        Module() = default;
        explicit Module(HMODULE value) noexcept : handle(value) {}
        Module(const Module&) = delete;
        Module& operator=(const Module&) = delete;

        Module(Module&& other) noexcept : handle(other.handle) {
            other.handle = nullptr;
        }

        Module& operator=(Module&& other) noexcept {
            if (this != &other) {
                Reset();
                handle = other.handle;
                other.handle = nullptr;
            }
            return *this;
        }

        ~Module() {
            Reset();
        }

        void Reset() noexcept {
            if (handle) {
                FreeLibrary(handle);
                handle = nullptr;
            }
        }
    };

    std::string BlobToString(ID3DBlob* blob) {
        if (!blob || blob->GetBufferSize() == 0) {
            return {};
        }

        const auto* data = static_cast<const char*>(blob->GetBufferPointer());
        return std::string(data, data + blob->GetBufferSize());
    }

    std::vector<std::byte> BlobToBytes(ID3DBlob* blob) {
        if (!blob || blob->GetBufferSize() == 0) {
            return {};
        }

        const auto* data = static_cast<const std::byte*>(blob->GetBufferPointer());
        return std::vector<std::byte>(data, data + blob->GetBufferSize());
    }

    bool ReadFileBytes(const std::filesystem::path& path, std::vector<char>& out) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            return false;
        }

        const std::streamoff size = file.tellg();
        if (size < 0) {
            return false;
        }

        out.resize(static_cast<std::size_t>(size));
        file.seekg(0);
        file.read(out.data(), size);
        return file.good() || size == 0;
    }

    class IncludeHandler final : public ID3DInclude {
      public:
        explicit IncludeHandler(std::vector<std::filesystem::path> roots) : m_roots(std::move(roots)) {}

        HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID, LPCVOID* data, UINT* bytes) override {
            if (!fileName || !data || !bytes) {
                return E_INVALIDARG;
            }

            const std::filesystem::path requested = ::util::Utf8ToWide(fileName);
            if (requested.empty()) {
                return E_FAIL;
            }

            auto buffer = std::make_unique<std::vector<char>>();
            if (!ReadFileBytes(Resolve(requested), *buffer)) {
                return E_FAIL;
            }

            *bytes = static_cast<UINT>(buffer->size());
            *data = buffer->data();
            m_openFiles.push_back(std::move(buffer));
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override {
            const auto it = std::ranges::find_if(m_openFiles, [data](const auto& buffer) { return buffer->data() == data; });
            if (it != m_openFiles.end()) {
                m_openFiles.erase(it);
            }
            return S_OK;
        }

      private:
        std::filesystem::path Resolve(const std::filesystem::path& requested) const {
            if (requested.is_absolute()) {
                return NormalizePath(requested);
            }

            for (const std::filesystem::path& root : m_roots) {
                std::filesystem::path candidate = NormalizePath(root / requested);
                std::error_code ec;
                if (std::filesystem::is_regular_file(candidate, ec)) {
                    return candidate;
                }
            }

            return NormalizePath(m_roots.empty() ? requested : m_roots.front() / requested);
        }

        std::vector<std::filesystem::path> m_roots;
        std::vector<std::unique_ptr<std::vector<char>>> m_openFiles;
    };

    CompileResult Failure(const CompileRequest& request, std::string diagnostics) {
        return CompileResult{
          .success = false,
          .bytecode = {},
          .diagnostics = std::move(diagnostics),
          .source_path = request.source_path,
          .generation = request.generation,
        };
    }

    D3DCompileFn LoadCompiler(Module& module) {
        module = Module{LoadLibraryExW(D3DCOMPILER_DLL_W, nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
        if (!module.handle) {
            return nullptr;
        }

        return reinterpret_cast<D3DCompileFn>(GetProcAddress(module.handle, "D3DCompile"));
    }
} // namespace

CompilerAvailability Compiler::CheckAvailability() {
    Module compilerModule;
    D3DCompileFn d3dCompile = LoadCompiler(compilerModule);
    if (!d3dCompile) {
        return CompilerAvailability{
          .available = false,
          .diagnostics = D3DCOMPILER_DLL_A " was not found beside HyprWin or in System32.",
        };
    }

    return CompilerAvailability{
      .available = true,
      .diagnostics = D3DCOMPILER_DLL_A " is available.",
    };
}

CompileResult Compiler::Compile(const CompileRequest& request) {
    Module compilerModule;
    D3DCompileFn d3dCompile = LoadCompiler(compilerModule);
    if (!d3dCompile) {
        return Failure(request, "Custom shader compilation requires " D3DCOMPILER_DLL_A " beside HyprWin or available on the system.");
    }

    const std::filesystem::path sourcePath = NormalizePath(request.source_path);
    std::vector<char> source;
    if (!ReadFileBytes(sourcePath, source)) {
        return Failure(request, std::format("Failed to read shader source: {}", sourcePath.string()));
    }

    const std::string sourceName = ::util::WideToUtf8(sourcePath.native());
    IncludeHandler includeHandler(std::vector<std::filesystem::path>{
      sourcePath.parent_path(),
      request.config_directory,
    });

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
    if (request.debug) {
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    } else {
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
    }

    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = d3dCompile(source.data(), source.size(), sourceName.c_str(), nullptr, &includeHandler, "ps_main", "ps_5_0", flags, 0, &bytecode, &errors);

    const std::string diagnostics = BlobToString(errors.Get());
    if (FAILED(hr)) {
        return Failure(request, diagnostics.empty() ? std::format("D3DCompile failed with HRESULT 0x{:08X}", static_cast<unsigned>(hr)) : diagnostics);
    }

    std::vector<std::byte> bytes = BlobToBytes(bytecode.Get());
    if (bytes.empty()) {
        return Failure(request, diagnostics.empty() ? "D3DCompile produced empty bytecode" : diagnostics);
    }

    return CompileResult{
      .success = true,
      .bytecode = Bytecode{std::move(bytes)},
      .diagnostics = diagnostics,
      .source_path = sourcePath,
      .generation = request.generation,
    };
}
} // namespace hw::outline
