#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

#include "util/geometry.hpp"

namespace win {
class SingleInstance {
  public:
    SingleInstance() noexcept = default;
    explicit SingleInstance(HANDLE mutex) noexcept;
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    static SingleInstance Create(const wchar_t* name) noexcept;

    bool IsValid() const noexcept;
    bool AlreadyRunning() const noexcept;
    DWORD Error() const noexcept;

  private:
    HANDLE m_mutex = nullptr;
    DWORD m_error = ERROR_SUCCESS;
};

bool SetProcessIdentity(const wchar_t* app_id) noexcept;
bool SetPerMonitorDpiAwareness() noexcept;
void DisableProcessThrottling() noexcept;

[[nodiscard]] vec::i4 GetVirtualScreenBounds() noexcept;
[[nodiscard]] std::filesystem::path GetModuleDirectory();
bool SetClipboardText(std::wstring_view text, HWND owner = nullptr) noexcept;
void ShowMessageBoxAsync(std::wstring text, std::wstring title, UINT flags) noexcept;

bool IsRunningAsAdmin() noexcept;
bool EnsureRunAsAdminAndExitIfNot() noexcept;

[[nodiscard]] bool ShellOpen(std::wstring_view file, std::wstring_view params = {}) noexcept;

[[nodiscard]] bool GetWorkAreaForWindow(HWND hwnd, RECT& workArea) noexcept;
[[nodiscard]] bool GetPrimaryWorkArea(RECT& workArea) noexcept;

bool RunProcess(std::wstring_view path, std::wstring_view args, std::wstring_view cwd = {}, bool admin = false);
bool LaunchApp(std::wstring_view path, std::wstring_view args, std::wstring_view cwd = {}, bool admin = false);

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

[[nodiscard]] WindowAtPointResult InspectWindowAtPoint(POINT pt) noexcept;
[[nodiscard]] HWND GetFilteredWindowAtPoint(POINT pt) noexcept;
[[nodiscard]] HWND GetFilteredWindowAtCursor() noexcept;
[[nodiscard]] bool IsWindowResponsive(HWND hwnd, DWORD timeoutMs = 200) noexcept;
[[nodiscard]] bool PostCloseWindow(HWND hwnd) noexcept;
[[nodiscard]] bool MinimizeWindow(HWND hwnd) noexcept;
[[nodiscard]] bool MaximizeWindow(HWND hwnd) noexcept;
[[nodiscard]] bool RestoreWindow(HWND hwnd) noexcept;
[[nodiscard]] DWORD GetProcessId(HWND hwnd) noexcept;
[[nodiscard]] std::wstring GetProcessNameByPid(DWORD pid);
[[nodiscard]] std::wstring GetProcessName(HWND hwnd);
[[nodiscard]] bool KillWindowProcess(HWND hwnd) noexcept;

void FocusWindow(HWND hwnd) noexcept;

[[nodiscard]] std::wstring GetWindowTitle(HWND hwnd);
[[nodiscard]] std::wstring GetWindowClass(HWND hwnd);
[[nodiscard]] std::vector<HWND> GetTopLevelWindows();

struct MonitorInfo {
    HMONITOR handle{};
    RECT rect{};
    RECT work_area{};
    bool is_primary = false;
    wchar_t name[32]{};
};
[[nodiscard]] std::vector<MonitorInfo> GetMonitors();
[[nodiscard]] MonitorInfo GetPrimaryMonitor() noexcept;
[[nodiscard]] MonitorInfo GetMonitorForWindow(HWND hwnd) noexcept;
[[nodiscard]] MonitorInfo GetMonitorAtPoint(LONG x, LONG y) noexcept;
bool GetMonitorDpi(HMONITOR handle, UINT& dpiX, UINT& dpiY) noexcept;
[[nodiscard]] bool SetMonitorResolution(const wchar_t* deviceName, int width, int height, int hz) noexcept;
} // namespace win
