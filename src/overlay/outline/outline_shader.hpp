#pragma once

#include <d3d11.h>

#include <cstddef>
#include <cstdint>

#include <wrl/client.h>

#include "config/settings.hpp"
#include "overlay/render/dx_context.hpp"
#include "overlay/outline/types.hpp"

namespace hw::outline {

enum class ShaderSession : std::uint32_t {
    None = 0,
    Drag = 1,
    Resize = 2,
};

struct alignas(16) ShaderRuntimeParams {
    float canvasSize[2];
    float rectCenter[2];

    float rectHalfSize[2];
    float gradientScale;
    std::uint32_t sessionType;

    float timeSeconds;
    float deltaSeconds;
    float sessionSeconds;
    float reserved;
};
static_assert(sizeof(ShaderRuntimeParams) == 48);

struct alignas(16) ShaderSettingsParams {
    float gradientDirection[2];
    float borderThickness;
    float cornerRadius;

    float outerAlpha;
    float glowFalloff;
    std::uint32_t colorCount;
    std::uint32_t reserved;

    float colors[kMaxShaderColors][4];
};
static_assert(sizeof(ShaderSettingsParams) == 288);

struct alignas(16) ShaderParams {
    ShaderRuntimeParams runtime;
    ShaderSettingsParams settings;
};
static_assert(sizeof(ShaderParams) == 336);
static_assert(offsetof(ShaderParams, runtime) == 0);
static_assert(offsetof(ShaderParams, settings) == 48);
static_assert(offsetof(ShaderRuntimeParams, rectHalfSize) == 16);
static_assert(offsetof(ShaderRuntimeParams, timeSeconds) == 32);
static_assert(offsetof(ShaderSettingsParams, outerAlpha) == 16);
static_assert(offsetof(ShaderSettingsParams, colors) == 32);

struct Shader {
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blendState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;

    [[nodiscard]] bool Create(DxContext& dx) noexcept;
    bool UseBuiltInPixelShader(DxContext& dx) noexcept;
    [[nodiscard]] bool InstallPixelShader(DxContext& dx, const Bytecode& bytecode) noexcept;
    bool UpdateConstants(DxContext& dx, const ShaderParams& params) noexcept;
    void Bind(DxContext& dx) noexcept;
    void Release() noexcept;
};

} // namespace hw::outline
