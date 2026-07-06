#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <memory>

#include <wrl/client.h>

namespace hw::thumbnail {

enum class WindowSnapshotStatus {
    Pending,
    Ready,
    Failed,
};

class WindowSnapshotCapture {
  public:
    WindowSnapshotCapture() noexcept;
    WindowSnapshotCapture(const WindowSnapshotCapture&) = delete;
    WindowSnapshotCapture& operator=(const WindowSnapshotCapture&) = delete;
    ~WindowSnapshotCapture();

    [[nodiscard]] WindowSnapshotStatus Begin(
      ID3D11Device* device,
      ID3D11DeviceContext* context,
      IDXGIDevice* dxgiDevice,
      HWND target) noexcept;
    [[nodiscard]] WindowSnapshotStatus Update(Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture) noexcept;
    void Cancel() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_apartmentInitialized = false;
    bool m_borderToggleAvailable = false;
    bool m_borderlessAccessRequested = false;
    bool m_borderlessAccessGranted = false;
};

} // namespace hw::thumbnail
