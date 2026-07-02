#include "overlay/overlay_window.hpp"

#include "log/log.hpp"
#include "win/native.hpp"

namespace hw {
namespace {
    constexpr wchar_t kOverlayWindowClass[] = L"HyprWinOverlayWindow";
} // namespace

OverlayWindow::~OverlayWindow() {
    Destroy();
}

bool OverlayWindow::Init(HINSTANCE instance) noexcept {
    if (m_hwnd) {
        return true;
    }

    if (!RegisterWNDClass(instance)) {
        return false;
    }

    const vec::i4 bounds = win::GetVirtualScreenBounds();

    m_hwnd = CreateWindowExW(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
      kOverlayWindowClass,
      L"",
      WS_POPUP,
      bounds.x,
      bounds.y,
      bounds.Width(),
      bounds.Height(),
      nullptr,
      nullptr,
      instance,
      nullptr);

    if (!m_hwnd) {
        LOG_ERROR("overlay_window: CreateWindowExW failed error={}", GetLastError());
        return false;
    }

    // Prevent Shell from treating the transparent full-screen overlay as a full-screen app.
    SetPropW(m_hwnd, L"NonRudeHWND", reinterpret_cast<HANDLE>(TRUE));
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_bounds = bounds;
    return true;
}

void OverlayWindow::Destroy() noexcept {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    m_bounds = {};
    m_visible = false;
}

void OverlayWindow::Show() noexcept {
    if (!m_hwnd || m_visible) {
        return;
    }

    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    // Explicit coordinates force DWM to re-evaluate HWND output assignment.
    // SWP_NOMOVE|SWP_NOSIZE does not trigger this after a per-output mode change.
    SetWindowPos(m_hwnd, HWND_TOPMOST, m_bounds.x, m_bounds.y, m_bounds.Width(), m_bounds.Height(), SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    m_visible = true;
}

void OverlayWindow::Hide() noexcept {
    if (!m_hwnd || !m_visible) {
        return;
    }

    ShowWindow(m_hwnd, SW_HIDE);
    m_visible = false;
}

bool OverlayWindow::UpdateVirtualDesktop() noexcept {
    if (!m_hwnd) {
        LOG_ERROR("overlay_window: UpdateVirtualDesktop called without hwnd");
        return false;
    }

    if (win::GetVirtualScreenBounds() == m_bounds) {
        return false;
    }
    ForceVirtualDesktopReapply();
    return true;
}

// Must run even when bounds are unchanged: an Hz-only mode change makes DWM
// silently drop the HWND output assignment; this SetWindowPos restores it.
void OverlayWindow::ForceVirtualDesktopReapply() noexcept {
    if (!m_hwnd) {
        return;
    }

    m_bounds = win::GetVirtualScreenBounds();
    SetWindowPos(m_hwnd, nullptr, m_bounds.x, m_bounds.y, m_bounds.Width(), m_bounds.Height(), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void OverlayWindow::PumpMessages() noexcept {
    if (!m_hwnd) {
        return;
    }

    MSG message{};
    while (PeekMessageW(&message, m_hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool OverlayWindow::ConsumeDisplayChanged() noexcept {
    return m_displayChanged.exchange(false, std::memory_order_acq_rel);
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_DISPLAYCHANGE || message == WM_DWMCOMPOSITIONCHANGED) {
        if (self) {
            self->m_displayChanged.store(true, std::memory_order_release);
        } else {
            LOG_ERROR("overlay_window: display change message {:#x} received before userdata was set", message);
        }
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool OverlayWindow::RegisterWNDClass(HINSTANCE instance) noexcept {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = &OverlayWindow::WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kOverlayWindowClass;

    if (RegisterClassExW(&wc) != 0) {
        return true;
    }

    const DWORD error = GetLastError();
    if (error != ERROR_CLASS_ALREADY_EXISTS) {
        LOG_ERROR("overlay_window: RegisterClassExW failed error={}", error);
    }
    return error == ERROR_CLASS_ALREADY_EXISTS;
}
} // namespace hw
