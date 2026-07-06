#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

namespace win {

[[nodiscard]] bool GetRawWindowRect(HWND hwnd, RECT& rawRect) noexcept;
[[nodiscard]] bool GetVisualWindowRect(HWND hwnd, RECT& visualRect) noexcept;
bool GetDwmVisualOffsets(HWND hwnd, RECT& offsets) noexcept;
void GetMinMaxInfo(HWND hwnd, SIZE& minSize, SIZE& maxSize) noexcept;
[[nodiscard]] bool GetBorderlessFullscreen(HWND hwnd, const RECT& rawRect) noexcept;
[[nodiscard]] bool GetResizable(HWND hwnd) noexcept;
[[nodiscard]] bool GetMaximized(HWND hwnd) noexcept;
[[nodiscard]] bool GetMinimized(HWND hwnd) noexcept;
[[nodiscard]] HWND GetForegroundWindowChecked() noexcept;
bool MoveWindowToRawRect(HWND hwnd, const RECT& rawRect) noexcept;
bool MoveWindowToVisualRect(HWND hwnd, const RECT& visualRect) noexcept;
bool PostMoveWindowToRawRect(HWND hwnd, const RECT& rawRect) noexcept;

enum class WindowFilterReason {
    None,
    Invalid,
    NoRoot,
    Minimized,
    Invisible,
    Cloaked,
    ShellProtected,
    Child,
    ToolWindow,
    NoActivate,
    NoProcess,
    RuleExcluded,
    OutsideVisualBounds,
};

struct WindowAtPointResult {
    HWND hit = nullptr;
    HWND top = nullptr;
    HWND candidate = nullptr;
    WindowFilterReason rejection = WindowFilterReason::None;
    std::string_view matched_rule;
    std::size_t matched_rule_index = static_cast<std::size_t>(-1);
};

[[nodiscard]] HWND GetFilteredWindowAtCursor() noexcept;
[[nodiscard]] bool IsWindowResponsive(HWND hwnd, DWORD timeoutMs = 200) noexcept;
[[nodiscard]] bool PostCloseWindow(HWND hwnd) noexcept;
[[nodiscard]] bool MinimizeWindow(HWND hwnd) noexcept;
[[nodiscard]] bool MaximizeWindow(HWND hwnd) noexcept;
[[nodiscard]] bool RestoreWindow(HWND hwnd) noexcept;
void FocusWindow(HWND hwnd) noexcept;

[[nodiscard]] std::wstring GetWindowTitle(HWND hwnd);
[[nodiscard]] std::wstring GetWindowClass(HWND hwnd);
[[nodiscard]] std::vector<HWND> GetTopLevelWindows();

} // namespace win
