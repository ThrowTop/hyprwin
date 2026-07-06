#include "win/window.hpp"

#include "log/log.hpp"
#include "win/app.hpp"
#include "win/window_internal.hpp"

#include <algorithm>

#include <dwmapi.h>

namespace win {
namespace {

    bool IsShellProtected(HWND hwnd) noexcept {
        const std::wstring windowClass = GetWindowClass(hwnd);
        if (windowClass == L"Shell_TrayWnd" || windowClass == L"Shell_SecondaryTrayWnd" || windowClass == L"Progman" || windowClass == L"WorkerW") {
            return true;
        }
        return windowClass == L"Windows.UI.Core.CoreWindow" && _wcsicmp(GetProcessName(hwnd).c_str(), L"StartMenuExperienceHost.exe") == 0;
    }

    bool IsCloaked(HWND hwnd) noexcept {
        BOOL cloaked = FALSE;
        return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
    }

    HWND TopLevel(HWND hwnd) noexcept {
        if (!IsWindow(hwnd)) {
            return nullptr;
        }

        HWND top = GetAncestor(hwnd, GA_ROOT);
        if (!top || !IsWindow(top) || IsIconic(top)) {
            return nullptr;
        }
        return detail::IsAppWindow(top) ? top : nullptr;
    }

    bool ContainsPointVisual(HWND hwnd, POINT point) noexcept {
        RECT rect{};
        return GetVisualWindowRect(hwnd, rect) && PtInRect(&rect, point);
    }

    struct HitTestContext {
        POINT point{};
        HWND result = nullptr;
    };

    BOOL CALLBACK FindWindowAtPointProc(HWND hwnd, LPARAM param) noexcept {
        auto& context = *reinterpret_cast<HitTestContext*>(param);
        HWND top = TopLevel(hwnd);
        if (!top) {
            return TRUE;
        }

        if (ContainsPointVisual(top, context.point)) {
            context.result = top;
            return FALSE;
        }
        return TRUE;
    }

    WindowAtPointResult InspectWindowAtPoint(POINT point) noexcept {
        WindowAtPointResult result{};
        result.hit = WindowFromPoint(point);
        if (!result.hit || !IsWindow(result.hit)) {
            result.rejection = WindowFilterReason::Invalid;
        } else {
            result.top = GetAncestor(result.hit, GA_ROOT);
            if (!result.top || !IsWindow(result.top)) {
                result.rejection = WindowFilterReason::NoRoot;
            } else if (IsIconic(result.top)) {
                result.rejection = WindowFilterReason::Minimized;
            } else if (detail::IsAppWindow(result.top, &result.rejection)) {
                if (ContainsPointVisual(result.top, point)) {
                    result.candidate = result.top;
                    return result;
                }
                result.rejection = WindowFilterReason::OutsideVisualBounds;
            }
        }

        if (result.rejection == WindowFilterReason::ShellProtected) {
            return result;
        }

        HitTestContext context{.point = point};
        EnumWindows(&FindWindowAtPointProc, reinterpret_cast<LPARAM>(&context));
        result.candidate = context.result;
        return result;
    }

} // namespace

namespace detail {

    bool IsAppWindow(HWND hwnd, WindowFilterReason* rejection) noexcept {
        const auto reject = [rejection](WindowFilterReason reason) noexcept {
            if (rejection) {
                *rejection = reason;
            }
            return false;
        };

        if (!IsWindowVisible(hwnd)) {
            return reject(WindowFilterReason::Invisible);
        }
        if (IsCloaked(hwnd)) {
            return reject(WindowFilterReason::Cloaked);
        }
        if (IsShellProtected(hwnd)) {
            return reject(WindowFilterReason::ShellProtected);
        }

        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if ((style & WS_CHILD) != 0) {
            return reject(WindowFilterReason::Child);
        }
        if ((exStyle & WS_EX_TOOLWINDOW) != 0) {
            return reject(WindowFilterReason::ToolWindow);
        }
        if ((exStyle & WS_EX_NOACTIVATE) != 0) {
            return reject(WindowFilterReason::NoActivate);
        }

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0) {
            return reject(WindowFilterReason::NoProcess);
        }
        if (rejection) {
            *rejection = WindowFilterReason::None;
        }
        return true;
    }

} // namespace detail

bool GetRawWindowRect(HWND hwnd, RECT& rawRect) noexcept {
    return IsWindow(hwnd) && GetWindowRect(hwnd, &rawRect) != FALSE;
}

bool GetVisualWindowRect(HWND hwnd, RECT& visualRect) noexcept {
    if (!IsWindow(hwnd)) {
        return false;
    }
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visualRect, sizeof(visualRect)))) {
        return true;
    }
    return GetRawWindowRect(hwnd, visualRect);
}

bool GetDwmVisualOffsets(HWND hwnd, RECT& offsets) noexcept {
    RECT raw{};
    RECT visual{};
    if (!GetRawWindowRect(hwnd, raw) || !GetVisualWindowRect(hwnd, visual)) {
        return false;
    }

    offsets = RECT{
      visual.left - raw.left,
      visual.top - raw.top,
      visual.right - raw.right,
      visual.bottom - raw.bottom,
    };
    return true;
}

void GetMinMaxInfo(HWND hwnd, SIZE& minSize, SIZE& maxSize) noexcept {
    if (!IsWindow(hwnd)) {
        return;
    }

    MINMAXINFO info{};
    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&info), SMTO_ABORTIFHUNG | SMTO_NORMAL, 100, &result) == 0) {
        minSize.cx = 100;
        minSize.cy = 38;
        maxSize.cx = 0;
        maxSize.cy = 0;
        return;
    }

    minSize.cx = info.ptMinTrackSize.x > 0 ? info.ptMinTrackSize.x : 100;
    minSize.cy = info.ptMinTrackSize.y > 0 ? info.ptMinTrackSize.y : 38;
    maxSize.cx = info.ptMaxTrackSize.x > 0 ? info.ptMaxTrackSize.x : 0;
    maxSize.cy = info.ptMaxTrackSize.y > 0 ? info.ptMaxTrackSize.y : 0;
}

bool GetBorderlessFullscreen(HWND hwnd, const RECT& rawRect) noexcept {
    MONITORINFO monitor{sizeof(monitor)};
    HMONITOR handle = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(handle, &monitor)) {
        return false;
    }

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const bool borderless = (style & WS_CAPTION) == 0 && (style & WS_THICKFRAME) == 0;
    return EqualRect(&rawRect, &monitor.rcMonitor) != FALSE && borderless;
}

bool GetResizable(HWND hwnd) noexcept {
    return IsWindow(hwnd) && (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0;
}

bool GetMaximized(HWND hwnd) noexcept {
    return IsWindow(hwnd) && IsZoomed(hwnd) != FALSE;
}

bool GetMinimized(HWND hwnd) noexcept {
    return IsWindow(hwnd) && IsIconic(hwnd) != FALSE;
}

HWND GetForegroundWindowChecked() noexcept {
    HWND hwnd = GetForegroundWindow();
    return IsWindow(hwnd) ? hwnd : nullptr;
}

// blocks: SetWindowPos sends WM_WINDOWPOSCHANGING/CHANGED to target message pump
bool MoveWindowToRawRect(HWND hwnd, const RECT& rawRect) noexcept {
    if (!IsWindow(hwnd)) {
        return false;
    }
    if (!IsWindowResponsive(hwnd)) {
        LOG_WARN("win: MoveWindowToRawRect: {:p} not responding", reinterpret_cast<void*>(hwnd));
        return false;
    }
    return SetWindowPos(hwnd, nullptr, rawRect.left, rawRect.top, rawRect.right - rawRect.left, rawRect.bottom - rawRect.top, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
}

bool MoveWindowToVisualRect(HWND hwnd, const RECT& visualRect) noexcept {
    RECT offsets{};
    if (!GetDwmVisualOffsets(hwnd, offsets)) {
        return MoveWindowToRawRect(hwnd, visualRect);
    }

    const RECT rawRect{
      visualRect.left - offsets.left,
      visualRect.top - offsets.top,
      visualRect.right - offsets.right,
      visualRect.bottom - offsets.bottom,
    };
    return MoveWindowToRawRect(hwnd, rawRect);
}

bool PostMoveWindowToRawRect(HWND hwnd, const RECT& rawRect) noexcept {
    return IsWindow(hwnd) &&
           SetWindowPos(
             hwnd,
             HWND_TOP,
             rawRect.left,
             rawRect.top,
             rawRect.right - rawRect.left,
             rawRect.bottom - rawRect.top,
             SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS) != FALSE;
}

HWND GetFilteredWindowAtCursor() noexcept {
    POINT point{};
    if (GetCursorPos(&point) == FALSE) {
        return nullptr;
    }
    return InspectWindowAtPoint(point).candidate;
}

bool PostCloseWindow(HWND hwnd) noexcept {
    return IsWindow(hwnd) && PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE;
}

bool IsWindowResponsive(HWND hwnd, DWORD timeoutMs) noexcept {
    DWORD_PTR result = 0;
    return SendMessageTimeout(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, timeoutMs, &result) != 0;
}

// blocks: ShowWindow sends WM_SIZE/WM_SHOWWINDOW to target message pump
bool MinimizeWindow(HWND hwnd) noexcept {
    if (!IsWindow(hwnd)) {
        return false;
    }
    if (!IsWindowResponsive(hwnd)) {
        LOG_WARN("win: MinimizeWindow: {:p} not responding", reinterpret_cast<void*>(hwnd));
        return false;
    }
    return ShowWindow(hwnd, SW_MINIMIZE) != FALSE;
}

// blocks: ShowWindow sends WM_SIZE/WM_SHOWWINDOW to target message pump
bool MaximizeWindow(HWND hwnd) noexcept {
    if (!IsWindow(hwnd)) {
        return false;
    }
    if (!IsWindowResponsive(hwnd)) {
        LOG_WARN("win: MaximizeWindow: {:p} not responding", reinterpret_cast<void*>(hwnd));
        return false;
    }
    return ShowWindow(hwnd, SW_MAXIMIZE) != FALSE;
}

// blocks: ShowWindow sends WM_SIZE/WM_SHOWWINDOW to target message pump
bool RestoreWindow(HWND hwnd) noexcept {
    if (!IsWindow(hwnd)) {
        return false;
    }
    if (!IsWindowResponsive(hwnd)) {
        LOG_WARN("win: RestoreWindow: {:p} not responding", reinterpret_cast<void*>(hwnd));
        return false;
    }
    return ShowWindow(hwnd, SW_RESTORE) != FALSE;
}

std::wstring GetWindowTitle(HWND hwnd) {
    if (!IsWindow(hwnd)) {
        return {};
    }
    wchar_t buffer[512]{};
    DWORD_PTR result = 0;
    SendMessageTimeout(hwnd, WM_GETTEXT, std::size(buffer), reinterpret_cast<LPARAM>(buffer), SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result);
    return result > 0 ? std::wstring(buffer, result) : std::wstring{};
}

std::wstring GetWindowClass(HWND hwnd) {
    if (!IsWindow(hwnd)) {
        return {};
    }
    wchar_t buffer[256]{};
    const int length = GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    return length > 0 ? std::wstring(buffer, static_cast<std::size_t>(length)) : std::wstring{};
}

std::vector<HWND> GetTopLevelWindows() {
    std::vector<HWND> result;
    EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
          if (detail::IsAppWindow(hwnd)) {
              reinterpret_cast<std::vector<HWND>*>(param)->push_back(hwnd);
          }
          return TRUE;
      },
      reinterpret_cast<LPARAM>(&result));
    return result;
}

void FocusWindow(HWND hwnd) noexcept {
    if (!IsWindow(hwnd)) {
        return;
    }

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    static_cast<void>(SendInput(1, &input, sizeof(input)));

    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = GetWindowThreadProcessId(foreground, nullptr);
    const DWORD targetThread = GetWindowThreadProcessId(hwnd, nullptr);
    const DWORD currentThread = GetCurrentThreadId();

    const bool attachForeground = foregroundThread != 0 && foregroundThread != currentThread && !IsHungAppWindow(foreground);
    const bool attachTarget = targetThread != 0 && targetThread != currentThread && targetThread != foregroundThread && !IsHungAppWindow(hwnd);

    if (attachForeground) {
        AttachThreadInput(currentThread, foregroundThread, TRUE);
    }
    if (attachTarget) {
        AttachThreadInput(currentThread, targetThread, TRUE);
    }

    AllowSetForegroundWindow(ASFW_ANY);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    if (attachTarget) {
        AttachThreadInput(currentThread, targetThread, FALSE);
    }
    if (attachForeground) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
}

} // namespace win
