#include "overlay/thumbnail/thumbnail_shader.hpp"

#include "log/log.hpp"
#include "overlay/thumbnail/window_snapshot.hpp"
#include "thumbnail_ps.h"
#include "thumbnail_vs.h"

namespace hw::thumbnail {
namespace {
    unsigned HrCode(HRESULT hr) noexcept {
        return static_cast<unsigned>(hr);
    }
} // namespace

bool ThumbnailShader::Create(DxContext& dx) noexcept {
    HRESULT hr = dx.device->CreateVertexShader(kThumbnailVS, sizeof(kThumbnailVS), nullptr, &m_vertexShader);
    if (FAILED(hr)) {
        LOG_ERROR("thumbnail_shader: CreateVertexShader failed hr=0x{:08X}", HrCode(hr));
        return false;
    }
    hr = dx.device->CreatePixelShader(kThumbnailPS, sizeof(kThumbnailPS), nullptr, &m_pixelShader);
    if (FAILED(hr)) {
        LOG_ERROR("thumbnail_shader: CreatePixelShader failed hr=0x{:08X}", HrCode(hr));
        return false;
    }

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    hr = dx.device->CreateSamplerState(&sampler, &m_sampler);
    if (FAILED(hr)) {
        LOG_ERROR("thumbnail_shader: CreateSamplerState failed hr=0x{:08X}", HrCode(hr));
        return false;
    }

    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(Constants);
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = dx.device->CreateBuffer(&buffer, nullptr, &m_constantBuffer);
    if (FAILED(hr)) {
        LOG_ERROR("thumbnail_shader: CreateBuffer failed hr=0x{:08X}", HrCode(hr));
        return false;
    }
    return true;
}

WindowSnapshotStatus ThumbnailShader::BeginCapture(DxContext& dx, HWND target) noexcept {
    Clear();
    return m_capture.Begin(dx.device.Get(), dx.context.Get(), dx.dxgiDevice.Get(), target);
}

WindowSnapshotStatus ThumbnailShader::UpdateCapture(DxContext& dx) noexcept {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    const WindowSnapshotStatus status = m_capture.Update(texture);
    if (status != WindowSnapshotStatus::Ready) {
        return status;
    }

    m_texture = std::move(texture);
    const HRESULT hr = dx.device->CreateShaderResourceView(m_texture.Get(), nullptr, &m_srv);
    if (FAILED(hr)) {
        LOG_WARN("thumbnail_shader: CreateShaderResourceView failed hr=0x{:08X}", HrCode(hr));
        Clear();
        return WindowSnapshotStatus::Failed;
    }

    return WindowSnapshotStatus::Ready;
}

void ThumbnailShader::CancelCapture() noexcept {
    m_capture.Cancel();
}

void ThumbnailShader::Draw(DxContext& dx, vec::i4 canvasBounds, vec::i4 screenBounds, float cornerRadius) noexcept {
    if (!m_srv) {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(dx.context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    auto& constants = *static_cast<Constants*>(mapped.pData);
    constants.rectPosition[0] = static_cast<float>(screenBounds.x - canvasBounds.x);
    constants.rectPosition[1] = static_cast<float>(screenBounds.y - canvasBounds.y);
    constants.rectSize[0] = static_cast<float>(screenBounds.Width());
    constants.rectSize[1] = static_cast<float>(screenBounds.Height());
    constants.cornerRadius = cornerRadius;
    dx.context->Unmap(m_constantBuffer.Get(), 0);

    dx.context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    dx.context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dx.context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    dx.context->PSSetShaderResources(0, 1, m_srv.GetAddressOf());
    dx.context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    dx.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dx.context->IASetInputLayout(nullptr);
    dx.context->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    dx.context->PSSetShaderResources(0, 1, &none);
}

void ThumbnailShader::Clear() noexcept {
    CancelCapture();
    m_srv.Reset();
    m_texture.Reset();
}

void ThumbnailShader::Release() noexcept {
    Clear();
    m_constantBuffer.Reset();
    m_sampler.Reset();
    m_pixelShader.Reset();
    m_vertexShader.Reset();
}

} // namespace hw::thumbnail
