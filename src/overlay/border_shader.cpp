#include "overlay/border_shader.hpp"

#include "border_ps.h"
#include "border_vs.h"
#include "log/log.hpp"

namespace hw {
namespace {
    unsigned HrCode(HRESULT hr) noexcept {
        return static_cast<unsigned>(hr);
    }
} // namespace

bool BorderShader::Create(DxContext& dx) noexcept {
    if (!dx.device) {
        LOG_ERROR("border_shader: Create called without D3D device");
        return false;
    }

    HRESULT hr = dx.device->CreateVertexShader(kBorderVS, sizeof(kBorderVS), nullptr, &vertexShader);
    if (FAILED(hr)) {
        LOG_ERROR("border_shader: CreateVertexShader failed hr=0x{:08X}", HrCode(hr));
        return false;
    }

    if (!UseBuiltInPixelShader(dx)) {
        LOG_ERROR("border_shader: built-in pixel shader creation failed");
        return false;
    }

    D3D11_BLEND_DESC blendDesc{};
    auto& rt = blendDesc.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_ONE;
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = dx.device->CreateBlendState(&blendDesc, &blendState);
    if (FAILED(hr)) {
        LOG_ERROR("border_shader: CreateBlendState failed hr=0x{:08X}", HrCode(hr));
        return false;
    }

    D3D11_RASTERIZER_DESC rastDesc{};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.DepthClipEnable = FALSE;

    hr = dx.device->CreateRasterizerState(&rastDesc, &rasterizerState);
    if (FAILED(hr)) {
        LOG_ERROR("border_shader: CreateRasterizerState failed hr=0x{:08X}", HrCode(hr));
        return false;
    }

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(ShaderParams);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = dx.device->CreateBuffer(&cbDesc, nullptr, &constantBuffer);
    if (FAILED(hr)) {
        LOG_ERROR("border_shader: CreateBuffer constants failed hr=0x{:08X}", HrCode(hr));
        return false;
    }
    return true;
}

bool BorderShader::UseBuiltInPixelShader(DxContext& dx) noexcept {
    if (!dx.device) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11PixelShader> next;
    const HRESULT hr = dx.device->CreatePixelShader(kBorderPS, sizeof(kBorderPS), nullptr, &next);
    if (FAILED(hr)) {
        return false;
    }
    pixelShader = std::move(next);
    return true;
}

bool BorderShader::InstallPixelShader(DxContext& dx, const shader::Bytecode& bytecode) noexcept {
    if (!dx.device || bytecode.bytes.empty()) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11PixelShader> next;
    const HRESULT hr = dx.device->CreatePixelShader(bytecode.bytes.data(), bytecode.bytes.size(), nullptr, &next);
    if (FAILED(hr)) {
        return false;
    }
    pixelShader = std::move(next);
    return true;
}

bool BorderShader::UpdateConstants(DxContext& dx, const ShaderParams& params) noexcept {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(dx.context->Map(constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }

    *static_cast<ShaderParams*>(mapped.pData) = params;
    dx.context->Unmap(constantBuffer.Get(), 0);
    return true;
}

void BorderShader::Bind(DxContext& dx) noexcept {
    dx.context->OMSetBlendState(blendState.Get(), nullptr, 0xffffffff);
    dx.context->RSSetState(rasterizerState.Get());
    dx.context->VSSetShader(vertexShader.Get(), nullptr, 0);
    dx.context->PSSetShader(pixelShader.Get(), nullptr, 0);
    dx.context->PSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
    dx.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dx.context->IASetInputLayout(nullptr);
}

void BorderShader::Release() noexcept {
    rasterizerState.Reset();
    blendState.Reset();
    constantBuffer.Reset();
    pixelShader.Reset();
    vertexShader.Reset();
}

} // namespace hw
