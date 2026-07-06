#pragma once

#include <vector>

#include <windows.h>

#include "util/geometry.hpp"

namespace win {

struct MonitorInfo {
    HMONITOR handle{};
    RECT rect{};
    RECT work_area{};
    bool is_primary = false;
    wchar_t name[32]{};
};

[[nodiscard]] vec::i4 GetVirtualScreenBounds() noexcept;
[[nodiscard]] bool GetWorkAreaForWindow(HWND hwnd, RECT& workArea) noexcept;
[[nodiscard]] bool GetPrimaryWorkArea(RECT& workArea) noexcept;
[[nodiscard]] std::vector<MonitorInfo> GetMonitors();
[[nodiscard]] MonitorInfo GetPrimaryMonitor() noexcept;
[[nodiscard]] MonitorInfo GetMonitorForWindow(HWND hwnd) noexcept;
[[nodiscard]] MonitorInfo GetMonitorAtPoint(LONG x, LONG y) noexcept;
bool GetMonitorDpi(HMONITOR handle, UINT& dpiX, UINT& dpiY) noexcept;
[[nodiscard]] bool SetMonitorResolution(const wchar_t* deviceName, int width, int height, int hz) noexcept;

} // namespace win
