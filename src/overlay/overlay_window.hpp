#pragma once

#include <atomic>

#include <windows.h>

#include "util/geometry.hpp"

namespace hw {
class OverlayWindow {
  public:
    OverlayWindow() = default;
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    ~OverlayWindow();

    [[nodiscard]] bool Init(HINSTANCE instance) noexcept;
    void Destroy() noexcept;

    void Show() noexcept;
    void Hide() noexcept;
    void PumpMessages() noexcept;
    bool UpdateVirtualDesktop() noexcept;
    void ForceVirtualDesktopReapply() noexcept;
    [[nodiscard]] bool ConsumeDisplayChanged() noexcept;

    [[nodiscard]] HWND hwnd() const noexcept {
        return m_hwnd;
    }
    [[nodiscard]] vec::i4 Bounds() const noexcept {
        return m_bounds;
    }

  private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    static bool RegisterWNDClass(HINSTANCE instance) noexcept;

    HWND m_hwnd = nullptr;
    vec::i4 m_bounds{};
    bool m_visible = false;
    std::atomic_bool m_displayChanged{false};
};
} // namespace hw