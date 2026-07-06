#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "overlay/thumbnail/window_snapshot.hpp"
#include "overlay/render/dx_context.hpp"
#include "util/geometry.hpp"

namespace hw::thumbnail {

class ThumbnailShader {
  public:
    [[nodiscard]] bool Create(DxContext& dx) noexcept;
    [[nodiscard]] WindowSnapshotStatus BeginCapture(DxContext& dx, HWND target) noexcept;
    [[nodiscard]] WindowSnapshotStatus UpdateCapture(DxContext& dx) noexcept;
    void CancelCapture() noexcept;
    void Draw(DxContext& dx, vec::i4 canvasBounds, vec::i4 screenBounds, float cornerRadius) noexcept;
    void Clear() noexcept;
    void Release() noexcept;

    [[nodiscard]] bool HasSnapshot() const noexcept {
        return m_texture != nullptr;
    }

  private:
    struct alignas(16) Constants {
        float rectPosition[2]{};
        float rectSize[2]{};
        float cornerRadius = 0.0f;
        float padding[3]{};
    };
    static_assert(sizeof(Constants) == 32);

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    WindowSnapshotCapture m_capture;
};

} // namespace hw::thumbnail
