#include "overlay/render/dx_context.hpp"
#include "log/log.hpp"
#include "util/debug.hpp"

#include <d3d11_4.h>
#include <dwmapi.h>

namespace hw {
namespace {
    unsigned HrCode(HRESULT hr) noexcept {
        return static_cast<unsigned>(hr);
    }
} // namespace

bool DxContext::Create(HWND hwndIn) noexcept {
    hwnd = hwndIn;
    Microsoft::WRL::ComPtr<ID3D11Multithread> multithread;

    const D3D_FEATURE_LEVEL featureLevels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
    };

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef HYPRWIN_RELEASE
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr)) {
        LOG_ERROR("dx: D3D11CreateDevice failed hr=0x{:08X}", HrCode(hr));
        goto fail;
    }

    hr = context.As(&multithread);
    if (FAILED(hr)) {
        LOG_ERROR("dx: query ID3D11Multithread failed hr=0x{:08X}", HrCode(hr));
        goto fail;
    }
    multithread->SetMultithreadProtected(TRUE);

    hr = device.As(&dxgiDevice);
    if (FAILED(hr)) {
        LOG_ERROR("dx: query IDXGIDevice2 failed hr=0x{:08X}", HrCode(hr));
        goto fail;
    }
    dxgiDevice->SetMaximumFrameLatency(1);

    hr = DCompositionCreateDevice(dxgiDevice.Get(), __uuidof(IDCompositionDevice), reinterpret_cast<void**>(dcompDevice.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("dx: DCompositionCreateDevice failed hr=0x{:08X}", HrCode(hr));
        goto fail;
    }

    hr = dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &dcompTarget);
    if (FAILED(hr)) {
        LOG_ERROR("dx: CreateTargetForHwnd failed hr=0x{:08X}", HrCode(hr));
        goto fail;
    }

    hr = dcompDevice->CreateVisual(&dcompVisual);
    if (FAILED(hr)) {
        LOG_ERROR("dx: CreateVisual failed hr=0x{:08X}", HrCode(hr));
        goto fail;
    }

    hr = dcompTarget->SetRoot(dcompVisual.Get());
    if (FAILED(hr)) {
        LOG_ERROR("dx: SetRoot failed hr=0x{:08X}", HrCode(hr));
        goto fail;
    }

    hr = dcompDevice->Commit();
    if (FAILED(hr)) {
        LOG_ERROR("dx: initial DComp Commit failed hr=0x{:08X}", HrCode(hr));
        goto fail;
    }
    return true;

fail:
    ReleaseDeviceResources();
    return false;
}

DxDeviceStatus DxContext::ValidateDevice() noexcept {
    if (!device) {
        LOG_ERROR("dx: ValidateDevice failed, no device");
        return DxDeviceStatus::Lost;
    }

    const HRESULT hr = device->GetDeviceRemovedReason();
    if (FAILED(hr)) {
        LOG_ERROR("dx: device removed reason hr=0x{:08X}", HrCode(hr));
        return DxDeviceStatus::Lost;
    }

    return DxDeviceStatus::Ok;
}

bool DxContext::EnsureSwapResources(UINT width, UINT height) noexcept {
    if (swapChain) {
        return true;
    }

    if (ValidateDevice() != DxDeviceStatus::Ok) {
        LOG_ERROR("dx: EnsureSwapResources blocked by lost device");
        ReleaseSwapResources();
        return false;
    }

    if (!CreateSwapChain(width, height) || !CreateRenderTargetView()) {
        LOG_ERROR("dx: creating swap resources failed {}x{}", width, height);
        ReleaseSwapResources();
        return false;
    }

    const HRESULT hr = dcompVisual->SetContent(swapChain.Get());
    if (FAILED(hr)) {
        LOG_ERROR("dx: DComp visual SetContent failed hr=0x{:08X}", HrCode(hr));
        ReleaseSwapResources();
        return false;
    }

    return true;
}

DxPresentStatus DxContext::PresentAndCommit(bool flushDwm) noexcept {
    HRESULT hr = swapChain->Present(0, 0);
    if (FAILED(hr)) {
        LOG_ERROR("dx: Present failed hr=0x{:08X}", HrCode(hr));
        return ValidateDevice() == DxDeviceStatus::Ok ? DxPresentStatus::SwapchainInvalid : DxPresentStatus::DeviceLost;
    }

    hr = dcompDevice->Commit();
    if (FAILED(hr)) {
        LOG_ERROR("dx: DComp Commit failed hr=0x{:08X}", HrCode(hr));
        return ValidateDevice() == DxDeviceStatus::Ok ? DxPresentStatus::SwapchainInvalid : DxPresentStatus::DeviceLost;
    }
    if (flushDwm) {
        const HRESULT flushHr = DwmFlush();
        if (FAILED(flushHr)) {
            LOG_ERROR("dx: DwmFlush failed hr=0x{:08X} -- DWM may have restarted", HrCode(flushHr));
        }
    }
    return DxPresentStatus::Ok;
}

void DxContext::ClearRenderTarget() noexcept {
    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->ClearRenderTargetView(rtv.Get(), clear);
    context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
}

void DxContext::SetViewport(UINT width, UINT height) noexcept {
    if (m_viewportValid && m_viewportW == width && m_viewportH == height) {
        return;
    }

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(width);
    vp.Height = static_cast<float>(height);
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);

    m_viewportW = width;
    m_viewportH = height;
    m_viewportValid = true;
}

void DxContext::ReleaseSwapResources() noexcept {
    if (context) {
        context->OMSetRenderTargets(0, nullptr, nullptr);
    }
    rtv.Reset();
    swapChain.Reset();
}

void DxContext::ReleaseDeviceResources() noexcept {
    // Cleanly disconnect the DComp visual tree before releasing resources so DWM
    // processes the disconnection synchronously rather than GC-ing it asynchronously.
    // This prevents DWM compositor state from accumulating across repeated runs.
    if (dcompDevice) {
        if (dcompVisual) {
            dcompVisual->SetContent(nullptr);
        }
        if (dcompTarget) {
            dcompTarget->SetRoot(nullptr);
        }
        dcompDevice->Commit();
    }

    ReleaseSwapResources();

    // Flush the GPU pipeline before releasing the context so the driver can reclaim
    // resources immediately rather than deferring them until the next submission.
    if (context) {
        context->ClearState();
        context->Flush();
    }
    m_viewportValid = false;
    m_viewportW = 0;
    m_viewportH = 0;

#ifndef HYPRWIN_RELEASE
    hw::debug::ReportLiveD3DObjects(device.Get());
#endif

    dcompVisual.Reset();
    dcompTarget.Reset();
    dcompDevice.Reset();

    dxgiDevice.Reset();
    context.Reset();
    device.Reset();
    hwnd = nullptr;
}

bool DxContext::CreateSwapChain(UINT width, UINT height) noexcept {
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    HRESULT hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) {
        LOG_ERROR("dx: GetAdapter failed hr=0x{:08X}", HrCode(hr));
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory2> factory2;
    hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory2.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("dx: GetParent IDXGIFactory2 failed hr=0x{:08X}", HrCode(hr));
        return false;
    }
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SampleDesc = {1, 0};
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = 0;

    hr = factory2->CreateSwapChainForComposition(device.Get(), &desc, nullptr, &swapChain);
    if (FAILED(hr)) {
        LOG_ERROR("dx: CreateSwapChainForComposition failed hr=0x{:08X} size={}x{}", HrCode(hr), width, height);
        return false;
    }

    return true;
}

bool DxContext::CreateRenderTargetView() noexcept {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("dx: swapChain GetBuffer failed hr=0x{:08X}", HrCode(hr));
        return false;
    }

    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

    hr = device->CreateRenderTargetView(backBuffer.Get(), &desc, &rtv);
    if (FAILED(hr)) {
        LOG_ERROR("dx: CreateRenderTargetView failed hr=0x{:08X}", HrCode(hr));
        return false;
    }

    return true;
}
} // namespace hw
