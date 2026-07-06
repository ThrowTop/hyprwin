#include "win/monitor.hpp"

#include <shellscalingapi.h>

namespace win {
namespace {

    MonitorInfo MonitorInfoFromHandle(HMONITOR handle) noexcept {
        MonitorInfo result{};
        result.handle = handle;

        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(handle, reinterpret_cast<MONITORINFO*>(&info)) != FALSE) {
            result.rect = info.rcMonitor;
            result.work_area = info.rcWork;
            result.is_primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            wcsncpy_s(result.name, info.szDevice, _TRUNCATE);
        }
        return result;
    }

} // namespace

vec::i4 GetVirtualScreenBounds() noexcept {
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return vec::i4{x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN), y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}

bool GetWorkAreaForWindow(HWND hwnd, RECT& workArea) noexcept {
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor) {
        return GetPrimaryWorkArea(workArea);
    }

    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(monitor, &info) == FALSE) {
        return false;
    }
    workArea = info.rcWork;
    return true;
}

bool GetPrimaryWorkArea(RECT& workArea) noexcept {
    workArea = RECT{};
    return SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) != FALSE;
}

std::vector<MonitorInfo> GetMonitors() {
    std::vector<MonitorInfo> result;
    EnumDisplayMonitors(
      nullptr,
      nullptr,
      [](HMONITOR monitor, HDC, LPRECT, LPARAM param) -> BOOL {
          reinterpret_cast<std::vector<MonitorInfo>*>(param)->push_back(MonitorInfoFromHandle(monitor));
          return TRUE;
      },
      reinterpret_cast<LPARAM>(&result));
    return result;
}

MonitorInfo GetPrimaryMonitor() noexcept {
    return MonitorInfoFromHandle(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));
}

MonitorInfo GetMonitorForWindow(HWND hwnd) noexcept {
    return MonitorInfoFromHandle(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
}

MonitorInfo GetMonitorAtPoint(LONG x, LONG y) noexcept {
    return MonitorInfoFromHandle(MonitorFromPoint({x, y}, MONITOR_DEFAULTTONEAREST));
}

bool GetMonitorDpi(HMONITOR handle, UINT& dpiX, UINT& dpiY) noexcept {
    return handle && SUCCEEDED(GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY));
}

bool SetMonitorResolution(const wchar_t* deviceName, int width, int height, int hz) noexcept {
    if (width <= 0 || height <= 0 || hz <= 0) {
        return false;
    }

    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    EnumDisplaySettingsW(deviceName, ENUM_CURRENT_SETTINGS, &mode);
    mode.dmPelsWidth = static_cast<DWORD>(width);
    mode.dmPelsHeight = static_cast<DWORD>(height);
    mode.dmBitsPerPel = 32;
    mode.dmDisplayFrequency = static_cast<DWORD>(hz);
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;

    return ChangeDisplaySettingsExW(deviceName, &mode, nullptr, CDS_UPDATEREGISTRY | CDS_GLOBAL, nullptr) == DISP_CHANGE_SUCCESSFUL;
}

} // namespace win
