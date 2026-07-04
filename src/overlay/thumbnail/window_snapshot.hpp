#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <wrl/client.h>

namespace hw::thumbnail {

struct WindowSnapshot {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    UINT width = 0;
    UINT height = 0;
};

class WindowSnapshotCapture {
  public:
    WindowSnapshotCapture() noexcept;
    WindowSnapshotCapture(const WindowSnapshotCapture&) = delete;
    WindowSnapshotCapture& operator=(const WindowSnapshotCapture&) = delete;
    ~WindowSnapshotCapture();

    [[nodiscard]] WindowSnapshot Capture(ID3D11Device* device, ID3D11DeviceContext* context, IDXGIDevice* dxgiDevice, HWND target) noexcept;

  private:
    bool m_apartmentInitialized = false;
    bool m_borderToggleAvailable = false;
    bool m_borderlessAccessRequested = false;
    bool m_borderlessAccessGranted = false;
};

} // namespace hw::thumbnail
