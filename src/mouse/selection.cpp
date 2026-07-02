#include "mouse/selection.hpp"

#include <array>

#include <dwmapi.h>

namespace hw::mouse {
namespace {
    struct BuiltInGrabFilterRule {
        std::string_view name;
        std::wstring_view process;
        std::wstring_view window_class;
    };

    constexpr std::array kBuiltInGrabFilterRules{
      BuiltInGrabFilterRule{"builtin:taskbar", {}, L"Shell_TrayWnd"},
      BuiltInGrabFilterRule{"builtin:secondary-taskbar", {}, L"Shell_SecondaryTrayWnd"},
      BuiltInGrabFilterRule{"builtin:desktop", {}, L"Progman"},
      BuiltInGrabFilterRule{"builtin:desktop-worker", {}, L"WorkerW"},
      BuiltInGrabFilterRule{"builtin:start-menu", L"StartMenuExperienceHost.exe", L"Windows.UI.Core.CoreWindow"},
    };

    bool EqualInsensitive(std::wstring_view left, std::wstring_view right) noexcept {
        return left.size() == right.size() && _wcsnicmp(left.data(), right.data(), left.size()) == 0;
    }
} // namespace

win::WindowAtPointResult SelectTarget(POINT pt, const Settings& settings) noexcept {
    win::WindowAtPointResult result{};
    result.hit = WindowFromPoint(pt);
    if (!result.hit || !IsWindow(result.hit)) {
        result.rejection = win::WindowFilterReason::Invalid;
        return result;
    }

    result.top = GetAncestor(result.hit, GA_ROOT);
    if (!result.top || !IsWindow(result.top)) {
        result.rejection = win::WindowFilterReason::NoRoot;
        return result;
    }
    if (IsIconic(result.top)) {
        result.rejection = win::WindowFilterReason::Minimized;
        return result;
    }
    if (!IsWindowVisible(result.top)) {
        result.rejection = win::WindowFilterReason::Invisible;
        return result;
    }

    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(result.top, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
        result.rejection = win::WindowFilterReason::Cloaked;
        return result;
    }

    const LONG_PTR style = GetWindowLongPtrW(result.top, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(result.top, GWL_EXSTYLE);
    if ((style & WS_CHILD) != 0) {
        result.rejection = win::WindowFilterReason::Child;
        return result;
    }
    if ((exStyle & WS_EX_TOOLWINDOW) != 0) {
        result.rejection = win::WindowFilterReason::ToolWindow;
        return result;
    }
    if ((exStyle & WS_EX_NOACTIVATE) != 0) {
        result.rejection = win::WindowFilterReason::NoActivate;
        return result;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(result.top, &pid);
    if (pid == 0) {
        result.rejection = win::WindowFilterReason::NoProcess;
        return result;
    }

    wchar_t classBuffer[256]{};
    const int classLength = GetClassNameW(result.top, classBuffer, static_cast<int>(std::size(classBuffer)));
    const std::wstring_view windowClass = classLength > 0 ? std::wstring_view{classBuffer, static_cast<std::size_t>(classLength)} : std::wstring_view{};

    wchar_t processBuffer[MAX_PATH]{};
    std::wstring_view process;
    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (processHandle) {
        DWORD processLength = MAX_PATH;
        if (QueryFullProcessImageNameW(processHandle, 0, processBuffer, &processLength) != FALSE) {
            const wchar_t* file = wcsrchr(processBuffer, L'\\');
            process = file ? std::wstring_view{file + 1} : std::wstring_view{processBuffer, processLength};
        }
        CloseHandle(processHandle);
    }

    GrabFilterAction decision = GrabFilterAction::Include;
    for (const BuiltInGrabFilterRule& rule : kBuiltInGrabFilterRules) {
        if ((rule.process.empty() || EqualInsensitive(process, rule.process)) && (rule.window_class.empty() || EqualInsensitive(windowClass, rule.window_class))) {
            decision = GrabFilterAction::Exclude;
            result.matched_rule = rule.name;
            result.matched_rule_index = static_cast<std::size_t>(-1);
        }
    }
    for (std::size_t i = 0; i < settings.grab_filters.size(); ++i) {
        const GrabFilterRule& rule = settings.grab_filters[i];
        if ((rule.process.empty() || EqualInsensitive(process, rule.process)) && (rule.window_class.empty() || EqualInsensitive(windowClass, rule.window_class))) {
            decision = rule.action;
            result.matched_rule = {};
            result.matched_rule_index = i;
        }
    }
    if (decision == GrabFilterAction::Exclude) {
        result.rejection = win::WindowFilterReason::RuleExcluded;
        return result;
    }

    RECT visualRect{};
    if (!win::GetVisualWindowRect(result.top, visualRect) || !PtInRect(&visualRect, pt)) {
        result.rejection = win::WindowFilterReason::OutsideVisualBounds;
        return result;
    }

    result.candidate = result.top;
    return result;
}
} // namespace hw::mouse
