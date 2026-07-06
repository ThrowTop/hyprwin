#pragma once

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <windows.h>

#include <wrl/client.h>

namespace hw {

enum class DxDeviceStatus {
    Ok,
    Lost,
};

enum class DxPresentStatus {
    Ok,
    SwapchainInvalid,
    DeviceLost,
};

struct DxContext {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGIDevice2> dxgiDevice;

    Microsoft::WRL::ComPtr<IDCompositionDevice> dcompDevice;
    Microsoft::WRL::ComPtr<IDCompositionTarget> dcompTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual> dcompVisual;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;

    HWND hwnd = nullptr;

    [[nodiscard]] bool Create(HWND hwndIn) noexcept;
    [[nodiscard]] DxDeviceStatus ValidateDevice() noexcept;
    [[nodiscard]] bool EnsureSwapResources(UINT width, UINT height) noexcept;
    [[nodiscard]] DxPresentStatus PresentAndCommit(bool flushDwm) noexcept;
    void ClearRenderTarget() noexcept;
    void SetViewport(UINT width, UINT height) noexcept;
    void ReleaseSwapResources() noexcept;
    void ReleaseDeviceResources() noexcept;

  private:
    [[nodiscard]] bool CreateSwapChain(UINT width, UINT height) noexcept;
    [[nodiscard]] bool CreateRenderTargetView() noexcept;

    UINT m_viewportW = 0;
    UINT m_viewportH = 0;
    bool m_viewportValid = false;
};

} // namespace hw
