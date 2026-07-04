#include "overlay/thumbnail/window_snapshot.hpp"

#include "log/log.hpp"

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>
#include <winrt/base.h>

#include <memory>

namespace hw::thumbnail {
namespace {
    using namespace winrt::Windows::Graphics;
    using namespace winrt::Windows::Graphics::Capture;
    using namespace winrt::Windows::Graphics::DirectX;
    using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

    constexpr DWORD kCaptureTimeoutMs = 250;

    const char* AccessStatusName(winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus status) noexcept {
        using winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus;
        switch (status) {
            case AppCapabilityAccessStatus::DeniedBySystem:
                return "DeniedBySystem";
            case AppCapabilityAccessStatus::NotDeclaredByApp:
                return "NotDeclaredByApp";
            case AppCapabilityAccessStatus::DeniedByUser:
                return "DeniedByUser";
            case AppCapabilityAccessStatus::UserPromptRequired:
                return "UserPromptRequired";
            case AppCapabilityAccessStatus::Allowed:
                return "Allowed";
        }
        return "Unknown";
    }

    struct FrameSignal {
        FrameSignal() noexcept : event(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
        ~FrameSignal() {
            if (event) {
                CloseHandle(event);
            }
        }
        HANDLE event = nullptr;
    };

    unsigned HrCode(HRESULT hr) noexcept {
        return static_cast<unsigned>(hr);
    }

    IDirect3DDevice CreateCaptureDevice(IDXGIDevice* dxgiDevice) {
        IDirect3DDevice result{nullptr};
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, reinterpret_cast<IInspectable**>(winrt::put_abi(result))));
        return result;
    }

    GraphicsCaptureItem CreateCaptureItem(HWND target) {
        const auto factory = winrt::get_activation_factory<GraphicsCaptureItem>();
        const auto interop = factory.as<IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem item{nullptr};
        winrt::check_hresult(interop->CreateForWindow(target, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
        return item;
    }

    WindowSnapshot CopyFrame(ID3D11Device* device, ID3D11DeviceContext* context, const Direct3D11CaptureFrame& frame) {
        const SizeInt32 contentSize = frame.ContentSize();
        if (contentSize.Width <= 0 || contentSize.Height <= 0) {
            return {};
        }

        const auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(source.GetAddressOf())));

        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        desc.Width = static_cast<UINT>(contentSize.Width);
        desc.Height = static_cast<UINT>(contentSize.Height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;

        WindowSnapshot result{};
        winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, &result.texture));

        const D3D11_BOX sourceBox{
          0,
          0,
          0,
          desc.Width,
          desc.Height,
          1,
        };
        context->CopySubresourceRegion(result.texture.Get(), 0, 0, 0, 0, source.Get(), 0, &sourceBox);
        result.width = desc.Width;
        result.height = desc.Height;
        return result;
    }
} // namespace

WindowSnapshotCapture::WindowSnapshotCapture() noexcept {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        m_apartmentInitialized = true;
        m_borderToggleAvailable = winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
          L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired");
        if (m_borderToggleAvailable) {
            LOG_INFO("window_snapshot: borderless capture property available=true");
        } else {
            LOG_WARN("window_snapshot: borderless capture property available=false; thumbnail capture disabled");
        }
    } catch (const winrt::hresult_error& error) {
        LOG_WARN("window_snapshot: apartment initialization failed hr=0x{:08X}", HrCode(error.code()));
    }
}

WindowSnapshotCapture::~WindowSnapshotCapture() {
    if (m_apartmentInitialized) {
        winrt::uninit_apartment();
    }
}

WindowSnapshot WindowSnapshotCapture::Capture(ID3D11Device* device, ID3D11DeviceContext* context, IDXGIDevice* dxgiDevice, HWND target) noexcept {
    if (!m_apartmentInitialized || !device || !context || !dxgiDevice || !IsWindow(target)) {
        return {};
    }

    const char* stage = "apartment";
    try {
        stage = "support";
        if (!GraphicsCaptureSession::IsSupported()) {
            return {};
        }

        if (!m_borderToggleAvailable) {
            return {};
        }

        if (!m_borderlessAccessRequested) {
            stage = "borderless-access";
            m_borderlessAccessRequested = true;
            const auto status = GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::Borderless).get();
            m_borderlessAccessGranted =
              status == winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus::Allowed;
            if (m_borderlessAccessGranted) {
                LOG_INFO("window_snapshot: borderless capture access status={}", AccessStatusName(status));
            } else {
                LOG_WARN("window_snapshot: borderless capture access status={}; thumbnail capture disabled", AccessStatusName(status));
            }
        }

        if (!m_borderlessAccessGranted) {
            return {};
        }

        stage = "device";
        const IDirect3DDevice captureDevice = CreateCaptureDevice(dxgiDevice);
        stage = "item";
        const GraphicsCaptureItem item = CreateCaptureItem(target);
        const SizeInt32 itemSize = item.Size();
        if (itemSize.Width <= 0 || itemSize.Height <= 0) {
            return {};
        }

        const auto signal = std::make_shared<FrameSignal>();
        if (!signal->event) {
            return {};
        }

        stage = "frame-pool";
        const Direct3D11CaptureFramePool framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(captureDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, itemSize);
        stage = "session";
        const GraphicsCaptureSession session = framePool.CreateCaptureSession(item);

        stage = "borderless-session";
        session.IsBorderRequired(false);

        stage = "event";
        const auto arrived =
          framePool.FrameArrived([signal](const Direct3D11CaptureFramePool&, const winrt::Windows::Foundation::IInspectable&) noexcept { SetEvent(signal->event); });

        stage = "start";
        session.IsCursorCaptureEnabled(false);
        session.StartCapture();

        WindowSnapshot result{};
        stage = "wait";
        if (WaitForSingleObject(signal->event, kCaptureTimeoutMs) == WAIT_OBJECT_0) {
            if (const Direct3D11CaptureFrame frame = framePool.TryGetNextFrame()) {
                stage = "copy";
                result = CopyFrame(device, context, frame);
                frame.Close();
            }
        } else {
            LOG_WARN("window_snapshot: capture timed out target={:p}", reinterpret_cast<void*>(target));
        }

        framePool.FrameArrived(arrived);
        session.Close();
        framePool.Close();

        return result;
    } catch (const winrt::hresult_error& error) {
        LOG_WARN("window_snapshot: capture failed target={:p} stage={} hr=0x{:08X}", reinterpret_cast<void*>(target), stage, HrCode(error.code()));
        return {};
    }
}

} // namespace hw::thumbnail
