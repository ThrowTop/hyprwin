#pragma once

#ifndef HYPRWIN_RELEASE

#include "log/log.hpp"
#include <d3d11.h>
#include <dxgidebug.h>
#include <wrl/client.h>

namespace hw::debug {
inline void ReportLiveD3DObjects(ID3D11Device* device) noexcept {
    if (!device)
        return;
    Microsoft::WRL::ComPtr<ID3D11Debug> dbg;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dbg)))) {
        LOG_WARN("dxdebug: ID3D11Debug unavailable");
        return;
    }
    LOG_DEBUG("dxdebug: reporting live D3D11 device objects");
    logging::flush();
    dbg->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
    LOG_DEBUG("dxdebug: D3D11 device-object report emitted to debugger output");
}

inline void ReportDxLiveObjects() noexcept {
    constexpr GUID kDxgiDebugAll{0xe48ae283, 0xda80, 0x490b, {0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x8}};
    LOG_DEBUG("dxdebug: reporting live DXGI objects");
    logging::flush();
    HMODULE mod = LoadLibraryW(L"dxgidebug.dll");
    if (!mod) {
        LOG_WARN("dxdebug: dxgidebug.dll not available");
        return;
    }
    using GetDebugFn = HRESULT(WINAPI*)(REFIID, void**);
    auto fn = reinterpret_cast<GetDebugFn>(GetProcAddress(mod, "DXGIGetDebugInterface"));
    if (!fn) {
        LOG_WARN("dxdebug: DXGIGetDebugInterface not available");
        FreeLibrary(mod);
        return;
    }
    Microsoft::WRL::ComPtr<IDXGIDebug1> dbg;
    if (FAILED(fn(IID_PPV_ARGS(&dbg)))) {
        LOG_WARN("dxdebug: DXGIGetDebugInterface failed");
        FreeLibrary(mod);
        return;
    }
    dbg->ReportLiveObjects(kDxgiDebugAll, DXGI_DEBUG_RLO_DETAIL);
    LOG_DEBUG("dxdebug: live-object report emitted to debugger output");
    dbg.Reset();
    FreeLibrary(mod);
}
} // namespace hw::debug

#endif // HYPRWIN_RELEASE
