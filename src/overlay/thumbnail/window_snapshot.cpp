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

#include <atomic>
#include <chrono>
#include <memory>

namespace hw::thumbnail {
namespace {
    using namespace winrt::Windows::Foundation;
    using namespace winrt::Windows::Graphics;
    using namespace winrt::Windows::Graphics::Capture;
    using namespace winrt::Windows::Graphics::DirectX;
    using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
    using namespace winrt::Windows::Security::Authorization::AppCapabilityAccess;

    constexpr auto kCaptureTimeout = std::chrono::milliseconds{100};

    const char* AccessStatusName(AppCapabilityAccessStatus status) noexcept {
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
        std::atomic_bool ready{false};
    };

    unsigned HrCode(HRESULT hr) noexcept {
        return static_cast<unsigned>(hr);
    }

    IDirect3DDevice CreateCaptureDevice(IDXGIDevice* dxgiDevice) {
        IDirect3DDevice result{nullptr};
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, reinterpret_cast<::IInspectable**>(winrt::put_abi(result))));
        return result;
    }

    GraphicsCaptureItem CreateCaptureItem(HWND target) {
        const auto factory = winrt::get_activation_factory<GraphicsCaptureItem>();
        const auto interop = factory.as<IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem item{nullptr};
        winrt::check_hresult(interop->CreateForWindow(target, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
        return item;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> CopyFrame(
      ID3D11Device* device,
      ID3D11DeviceContext* context,
      const Direct3D11CaptureFrame& frame) {
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

        Microsoft::WRL::ComPtr<ID3D11Texture2D> result;
        winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, &result));

        const D3D11_BOX sourceBox{
          0,
          0,
          0,
          desc.Width,
          desc.Height,
          1,
        };
        context->CopySubresourceRegion(result.Get(), 0, 0, 0, 0, source.Get(), 0, &sourceBox);
        return result;
    }
} // namespace

struct WindowSnapshotCapture::Impl {
    enum class Phase {
        Idle,
        Permission,
        Frame,
    };

    Phase phase = Phase::Idle;
    HWND target = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    IAsyncOperation<AppCapabilityAccessStatus> accessOperation{nullptr};
    IDirect3DDevice captureDevice{nullptr};
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool framePool{nullptr};
    GraphicsCaptureSession session{nullptr};
    winrt::event_token frameArrivedToken{};
    std::shared_ptr<FrameSignal> frameSignal;
    std::chrono::steady_clock::time_point deadline{};
};

WindowSnapshotCapture::WindowSnapshotCapture() noexcept : m_impl(std::make_unique<Impl>()) {
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
    Cancel();
    m_impl->accessOperation = nullptr;
    if (m_apartmentInitialized) {
        winrt::uninit_apartment();
    }
}

WindowSnapshotStatus WindowSnapshotCapture::Begin(
  ID3D11Device* device,
  ID3D11DeviceContext* context,
  IDXGIDevice* dxgiDevice,
  HWND target) noexcept {
    Cancel();
    if (!m_apartmentInitialized || !m_borderToggleAvailable || !device || !context || !dxgiDevice || !IsWindow(target)) {
        return WindowSnapshotStatus::Failed;
    }

    m_impl->target = target;
    m_impl->device = device;
    m_impl->context = context;
    m_impl->dxgiDevice = dxgiDevice;

    if (m_borderlessAccessGranted) {
        m_impl->phase = Impl::Phase::Permission;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> ignored;
        return Update(ignored);
    }

    if (!m_borderlessAccessRequested) {
        try {
            m_impl->accessOperation = GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::Borderless);
            m_borderlessAccessRequested = true;
        } catch (const winrt::hresult_error& error) {
            LOG_WARN("window_snapshot: borderless access request failed hr=0x{:08X}", HrCode(error.code()));
            Cancel();
            return WindowSnapshotStatus::Failed;
        }
    }

    if (!m_impl->accessOperation) {
        Cancel();
        return WindowSnapshotStatus::Failed;
    }

    m_impl->phase = Impl::Phase::Permission;
    return WindowSnapshotStatus::Pending;
}

WindowSnapshotStatus WindowSnapshotCapture::Update(Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture) noexcept {
    texture.Reset();
    if (m_impl->phase == Impl::Phase::Idle) {
        return WindowSnapshotStatus::Failed;
    }

    const char* stage = "permission";
    try {
        if (m_impl->phase == Impl::Phase::Permission) {
            if (!m_borderlessAccessGranted) {
                if (!m_impl->accessOperation || m_impl->accessOperation.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
                    return WindowSnapshotStatus::Pending;
                }
                if (m_impl->accessOperation.Status() != winrt::Windows::Foundation::AsyncStatus::Completed) {
                    LOG_WARN("window_snapshot: borderless capture access request did not complete");
                    Cancel();
                    return WindowSnapshotStatus::Failed;
                }

                const AppCapabilityAccessStatus status = m_impl->accessOperation.GetResults();
                m_impl->accessOperation = nullptr;
                m_borderlessAccessGranted = status == AppCapabilityAccessStatus::Allowed;
                if (m_borderlessAccessGranted) {
                    LOG_INFO("window_snapshot: borderless capture access status={}", AccessStatusName(status));
                } else {
                    LOG_WARN("window_snapshot: borderless capture access status={}; thumbnail capture disabled", AccessStatusName(status));
                    Cancel();
                    return WindowSnapshotStatus::Failed;
                }
            }

            stage = "support";
            if (!GraphicsCaptureSession::IsSupported()) {
                Cancel();
                return WindowSnapshotStatus::Failed;
            }

            stage = "device";
            m_impl->captureDevice = CreateCaptureDevice(m_impl->dxgiDevice.Get());
            stage = "item";
            m_impl->item = CreateCaptureItem(m_impl->target);
            const SizeInt32 itemSize = m_impl->item.Size();
            if (itemSize.Width <= 0 || itemSize.Height <= 0) {
                Cancel();
                return WindowSnapshotStatus::Failed;
            }

            stage = "frame-pool";
            m_impl->framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
              m_impl->captureDevice,
              DirectXPixelFormat::B8G8R8A8UIntNormalized,
              1,
              itemSize);
            stage = "session";
            m_impl->session = m_impl->framePool.CreateCaptureSession(m_impl->item);
            m_impl->session.IsBorderRequired(false);
            m_impl->session.IsCursorCaptureEnabled(false);

            m_impl->frameSignal = std::make_shared<FrameSignal>();
            const std::shared_ptr<FrameSignal> signal = m_impl->frameSignal;
            m_impl->frameArrivedToken =
              m_impl->framePool.FrameArrived([signal](
                                               const Direct3D11CaptureFramePool&,
                                               const winrt::Windows::Foundation::IInspectable&) noexcept {
                  signal->ready.store(true, std::memory_order_release);
              });

            stage = "start";
            m_impl->session.StartCapture();
            m_impl->deadline = std::chrono::steady_clock::now() + kCaptureTimeout;
            m_impl->phase = Impl::Phase::Frame;
        }

        if (m_impl->frameSignal->ready.load(std::memory_order_acquire)) {
            if (const Direct3D11CaptureFrame frame = m_impl->framePool.TryGetNextFrame()) {
                stage = "copy";
                texture = CopyFrame(m_impl->device.Get(), m_impl->context.Get(), frame);
                frame.Close();
                Cancel();
                return texture ? WindowSnapshotStatus::Ready : WindowSnapshotStatus::Failed;
            }
        }

        if (std::chrono::steady_clock::now() >= m_impl->deadline) {
            LOG_WARN("window_snapshot: capture timed out target={:p}", reinterpret_cast<void*>(m_impl->target));
            Cancel();
            return WindowSnapshotStatus::Failed;
        }
        return WindowSnapshotStatus::Pending;
    } catch (const winrt::hresult_error& error) {
        LOG_WARN("window_snapshot: capture failed target={:p} stage={} hr=0x{:08X}",
          reinterpret_cast<void*>(m_impl->target),
          stage,
          HrCode(error.code()));
        Cancel();
        return WindowSnapshotStatus::Failed;
    }
}

void WindowSnapshotCapture::Cancel() noexcept {
    try {
        if (m_impl->framePool) {
            if (m_impl->frameArrivedToken.value != 0) {
                m_impl->framePool.FrameArrived(m_impl->frameArrivedToken);
            }
            if (m_impl->session) {
                m_impl->session.Close();
            }
            m_impl->framePool.Close();
        }
    } catch (const winrt::hresult_error& error) {
        LOG_WARN("window_snapshot: capture cleanup failed hr=0x{:08X}", HrCode(error.code()));
    }

    m_impl->phase = Impl::Phase::Idle;
    m_impl->target = nullptr;
    m_impl->device.Reset();
    m_impl->context.Reset();
    m_impl->dxgiDevice.Reset();
    m_impl->captureDevice = nullptr;
    m_impl->item = nullptr;
    m_impl->framePool = nullptr;
    m_impl->session = nullptr;
    m_impl->frameArrivedToken = {};
    m_impl->frameSignal.reset();
    m_impl->deadline = {};
}

} // namespace hw::thumbnail
