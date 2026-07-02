#include "win/native.hpp"

#include "log/log.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>

#include <ole2.h>
#include <oleauto.h>

#include <dwmapi.h>
#include <exdisp.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shldisp.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <userenv.h>
#include <windows.h>
#include <wrl/client.h>

namespace win {
namespace {
    using Microsoft::WRL::ComPtr;

    void CloseHandleIfValid(HANDLE handle) noexcept {
        if (handle) {
            CloseHandle(handle);
        }
    }

    class CoInit {
      public:
        CoInit() noexcept : m_hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
        ~CoInit() {
            if (SUCCEEDED(m_hr)) {
                CoUninitialize();
            }
        }

        bool Ok() const noexcept {
            return SUCCEEDED(m_hr);
        }

      private:
        HRESULT m_hr = E_FAIL;
    };

    class OleVariant {
      public:
        explicit OleVariant(const wchar_t* value) {
            m_value.vt = VT_BSTR;
            m_value.bstrVal = SysAllocString(value);
        }

        explicit OleVariant(std::wstring_view value) {
            const std::wstring copy{value};
            m_value.vt = VT_BSTR;
            m_value.bstrVal = SysAllocString(copy.c_str());
        }

        explicit OleVariant(long value) noexcept {
            m_value.vt = VT_I4;
            m_value.lVal = value;
        }

        ~OleVariant() {
            VariantClear(&m_value);
        }

        OleVariant(const OleVariant&) = delete;
        OleVariant& operator=(const OleVariant&) = delete;

        VARIANT& Get() noexcept {
            return m_value;
        }

      private:
        VARIANT m_value{};
    };

    class OleBStr {
      public:
        explicit OleBStr(std::wstring_view value) {
            const std::wstring copy{value};
            m_value = SysAllocString(copy.c_str());
        }

        ~OleBStr() {
            SysFreeString(m_value);
        }

        OleBStr(const OleBStr&) = delete;
        OleBStr& operator=(const OleBStr&) = delete;

        BSTR Get() const noexcept {
            return m_value;
        }

      private:
        BSTR m_value = nullptr;
    };

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

    // Shared filter for user-facing app windows. Deliberately does NOT exclude
    // minimized windows: hit-testing (TopLevel) adds that check, while window
    // enumeration must still report them.
    bool IsAppWindow(HWND hwnd, WindowFilterReason* rejection = nullptr) noexcept {
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

    HWND TopLevel(HWND hwnd) noexcept {
        if (!IsWindow(hwnd)) {
            return nullptr;
        }

        HWND top = GetAncestor(hwnd, GA_ROOT);
        if (!top || !IsWindow(top) || IsIconic(top)) {
            return nullptr;
        }

        return IsAppWindow(top) ? top : nullptr;
    }

    bool ContainsPointVisual(HWND hwnd, POINT pt) noexcept {
        RECT rect{};
        return GetVisualWindowRect(hwnd, rect) && PtInRect(&rect, pt);
    }

    struct HitTestContext {
        POINT pt{};
        HWND result = nullptr;
    };

    struct LaunchWindowContext {
        DWORD pid = 0;
        const std::vector<HWND>* existing = nullptr;
        HWND result = nullptr;
    };

    BOOL CALLBACK FindWindowAtPointProc(HWND hwnd, LPARAM lparam) noexcept {
        auto& context = *reinterpret_cast<HitTestContext*>(lparam);
        HWND top = TopLevel(hwnd);
        if (!top) {
            return TRUE;
        }

        if (ContainsPointVisual(top, context.pt)) {
            context.result = top;
            return FALSE;
        }

        return TRUE;
    }

    BOOL CALLBACK FindLaunchWindowProc(HWND hwnd, LPARAM lparam) noexcept {
        auto& context = *reinterpret_cast<LaunchWindowContext*>(lparam);
        if (!IsAppWindow(hwnd)) {
            return TRUE;
        }

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == context.pid) {
            context.result = hwnd;
            return FALSE;
        }

        if (context.existing && std::find(context.existing->begin(), context.existing->end(), hwnd) == context.existing->end()) {
            context.result = hwnd;
            return FALSE;
        }

        return TRUE;
    }

    HWND FindLaunchWindow(DWORD pid, const std::vector<HWND>* existing) noexcept {
        if (pid == 0 && !existing) {
            return nullptr;
        }

        LaunchWindowContext context{.pid = pid, .existing = existing};
        EnumWindows(&FindLaunchWindowProc, reinterpret_cast<LPARAM>(&context));
        return context.result;
    }

    struct ProcessLaunch {
        bool ok = false;
        DWORD pid = 0;
        HANDLE process = nullptr;
    };

    thread_local LaunchWindowContext* g_launchWindowContext = nullptr;

    void CALLBACK LaunchWindowEventProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG objectId, LONG childId, DWORD, DWORD) noexcept {
        LaunchWindowContext* context = g_launchWindowContext;
        if (!context || context->result || objectId != OBJID_WINDOW || childId != CHILDID_SELF || !IsAppWindow(hwnd)) {
            return;
        }

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == context->pid || (context->existing && std::find(context->existing->begin(), context->existing->end(), hwnd) == context->existing->end())) {
            context->result = hwnd;
        }
    }

    HWND WaitForLaunchWindow(DWORD pid, HANDLE process, const std::vector<HWND>& existing, DWORD timeoutMs) noexcept {
        HWND hwnd = FindLaunchWindow(pid, &existing);
        if (hwnd || timeoutMs == 0) {
            return hwnd;
        }

        LaunchWindowContext context{.pid = pid, .existing = &existing};
        g_launchWindowContext = &context;
        HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, LaunchWindowEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        HANDLE waitProcess = process;
        while (!context.result) {
            context.result = FindLaunchWindow(pid, &existing);
            if (context.result) {
                break;
            }

            const ULONGLONG now = GetTickCount64();
            if (now >= deadline) {
                break;
            }

            const DWORD waitMs = static_cast<DWORD>(deadline - now);
            const DWORD handleCount = waitProcess ? 1UL : 0UL;
            HANDLE handles[] = {waitProcess};
            const DWORD wait = MsgWaitForMultipleObjectsEx(handleCount, handles, waitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            if (waitProcess && wait == WAIT_OBJECT_0) {
                waitProcess = nullptr;
            }
        }

        if (hook) {
            UnhookWinEvent(hook);
        }
        g_launchWindowContext = nullptr;
        return context.result;
    }

    ProcessLaunch RunAsShellUserTracked(std::wstring_view path, std::wstring_view args, std::wstring_view cwd) {
        DWORD pid = 0;
        HWND shell = GetShellWindow();
        GetWindowThreadProcessId(shell, &pid);
        if (pid == 0) {
            return {};
        }

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) {
            return {};
        }

        HANDLE token = nullptr;
        if (OpenProcessToken(process, TOKEN_DUPLICATE | TOKEN_QUERY, &token) == FALSE) {
            CloseHandle(process);
            return {};
        }

        HANDLE primary = nullptr;
        if (DuplicateTokenEx(token, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &primary) == FALSE) {
            CloseHandle(token);
            CloseHandle(process);
            return {};
        }

        std::wstring command = L"\"";
        command.append(path);
        command += L"\"";
        if (!args.empty()) {
            command += L" ";
            command.append(args);
        }

        STARTUPINFOW startup{.cb = sizeof(startup)};
        PROCESS_INFORMATION info{};
        LPVOID environment = nullptr;
        static_cast<void>(CreateEnvironmentBlock(&environment, primary, FALSE));

        const std::wstring cwd_copy{cwd};
        const BOOL ok = CreateProcessWithTokenW(
          primary, LOGON_WITH_PROFILE, nullptr, command.data(), CREATE_UNICODE_ENVIRONMENT, environment, cwd_copy.empty() ? nullptr : cwd_copy.c_str(), &startup, &info);

        ProcessLaunch result{};
        if (ok != FALSE) {
            CloseHandle(info.hThread);
            result.ok = true;
            result.pid = info.dwProcessId;
            result.process = info.hProcess;
        }

        if (environment) {
            DestroyEnvironmentBlock(environment);
        }
        CloseHandle(primary);
        CloseHandle(token);
        CloseHandle(process);
        return result;
    }

    bool RunAsShellUser(std::wstring_view path, std::wstring_view args, std::wstring_view cwd) {
        ProcessLaunch result = RunAsShellUserTracked(path, args, cwd);
        CloseHandleIfValid(result.process);
        return result.ok;
    }

    ProcessLaunch RunViaShellTracked(std::wstring_view path, std::wstring_view args, std::wstring_view cwd, const wchar_t* verb) {
        const std::wstring path_copy{path};
        const std::wstring args_copy{args};
        const std::wstring cwd_copy{cwd};

        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = verb;
        info.lpFile = path_copy.c_str();
        info.lpParameters = args_copy.empty() ? nullptr : args_copy.c_str();
        info.lpDirectory = cwd_copy.empty() ? nullptr : cwd_copy.c_str();
        info.nShow = SW_SHOWNORMAL;

        if (ShellExecuteExW(&info) == FALSE) {
            return {};
        }

        ProcessLaunch result{.ok = true, .process = info.hProcess};
        if (info.hProcess) {
            result.pid = ::GetProcessId(info.hProcess);
        }
        return result;
    }

    bool RunViaShell(std::wstring_view path, std::wstring_view args, std::wstring_view cwd, const wchar_t* verb) {
        const std::wstring path_copy{path};
        const std::wstring args_copy{args};
        const std::wstring cwd_copy{cwd};

        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.lpVerb = verb;
        info.lpFile = path_copy.c_str();
        info.lpParameters = args_copy.empty() ? nullptr : args_copy.c_str();
        info.lpDirectory = cwd_copy.empty() ? nullptr : cwd_copy.c_str();
        info.nShow = SW_SHOWNORMAL;
        return ShellExecuteExW(&info) != FALSE;
    }

    bool GetDesktopAutomationObject(REFIID riid, void** result) {
        *result = nullptr;

        ComPtr<IShellWindows> shell_windows;
        HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(shell_windows.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        VARIANT empty{};
        OleVariant loc(static_cast<long>(CSIDL_DESKTOP));

        long hwnd = 0;
        ComPtr<IDispatch> dispatch;
        hr = shell_windows.Get()->FindWindowSW(&loc.Get(), &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, dispatch.GetAddressOf());
        if (FAILED(hr) || !dispatch.Get()) {
            return false;
        }

        ComPtr<IServiceProvider> provider;
        hr = dispatch.Get()->QueryInterface(IID_PPV_ARGS(provider.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IShellBrowser> browser;
        hr = provider.Get()->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(browser.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IShellView> view;
        hr = browser.Get()->QueryActiveShellView(view.GetAddressOf());
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IDispatch> view_dispatch;
        hr = view.Get()->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(view_dispatch.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IShellFolderViewDual> folder_view;
        hr = view_dispatch.Get()->QueryInterface(IID_PPV_ARGS(folder_view.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IDispatch> app_dispatch;
        hr = folder_view.Get()->get_Application(app_dispatch.GetAddressOf());
        if (FAILED(hr)) {
            return false;
        }

        return SUCCEEDED(app_dispatch.Get()->QueryInterface(riid, result));
    }

    bool RunViaExplorerShell(std::wstring_view path, std::wstring_view args, std::wstring_view cwd) {
        CoInit com;
        if (!com.Ok()) {
            return false;
        }

        ComPtr<IShellDispatch2> shell;
        if (!GetDesktopAutomationObject(IID_PPV_ARGS(shell.GetAddressOf()))) {
            return false;
        }

        OleBStr file(path);
        OleVariant v_args(args);
        OleVariant v_dir(cwd);
        OleVariant v_operation(L"open");
        OleVariant v_show(static_cast<long>(SW_SHOWNORMAL));

        return SUCCEEDED(shell.Get()->ShellExecuteW(file.Get(), v_args.Get(), v_dir.Get(), v_operation.Get(), v_show.Get()));
    }
} // namespace

SingleInstance::SingleInstance(HANDLE mutex) noexcept : m_mutex(mutex), m_error(GetLastError()) {}

SingleInstance::~SingleInstance() {
    CloseHandleIfValid(m_mutex);
}

SingleInstance SingleInstance::Create(const wchar_t* name) noexcept {
    return SingleInstance(CreateMutexW(nullptr, FALSE, name));
}

bool SingleInstance::IsValid() const noexcept {
    return m_mutex != nullptr;
}

bool SingleInstance::AlreadyRunning() const noexcept {
    return m_error == ERROR_ALREADY_EXISTS;
}

DWORD SingleInstance::Error() const noexcept {
    return m_error;
}

bool SetProcessIdentity(const wchar_t* app_id) noexcept {
    return SUCCEEDED(SetCurrentProcessExplicitAppUserModelID(app_id));
}

bool SetPerMonitorDpiAwareness() noexcept {
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        return true;
    }

    return SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE) != FALSE;
}

void DisableProcessThrottling() noexcept {
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
}

vec::i4 GetVirtualScreenBounds() noexcept {
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return vec::i4{x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN), y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}

std::filesystem::path GetModuleDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    while (true) {
        const DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) {
            return {};
        }
        if (len < static_cast<DWORD>(path.size())) {
            path.resize(len);
            break;
        }
        path.resize(path.size() * 2);
    }
    return std::filesystem::path(path).parent_path();
}

bool SetClipboardText(std::wstring_view text, HWND owner) noexcept {
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        return false;
    }

    auto* dest = static_cast<wchar_t*>(GlobalLock(memory));
    if (!dest) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(dest, text.data(), text.size() * sizeof(wchar_t));
    dest[text.size()] = L'\0';
    GlobalUnlock(memory);

    if (!OpenClipboard(owner)) {
        GlobalFree(memory);
        return false;
    }
    EmptyClipboard();
    const bool ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    if (!ok) {
        GlobalFree(memory);
    }
    CloseClipboard();
    return ok;
}

void ShowMessageBoxAsync(std::wstring text, std::wstring title, UINT flags) noexcept {
    // One box at a time: a config error loop must not spawn unbounded blocked threads.
    static std::atomic_bool active{false};
    if (active.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    try {
        std::thread([text = std::move(text), title = std::move(title), flags] {
            SET_THREAD_NAME("MSG");
            MessageBoxW(nullptr, text.c_str(), title.c_str(), flags);
            active.store(false, std::memory_order_release);
        }).detach();
    } catch (...) {
        active.store(false, std::memory_order_release);
    }
}

bool IsRunningAsAdmin() noexcept {
    BOOL is_admin = FALSE;
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    PSID admin_group = nullptr;

    if (AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_group)) {
        CheckTokenMembership(nullptr, admin_group, &is_admin);
        FreeSid(admin_group);
    }

    return is_admin != FALSE;
}

bool EnsureRunAsAdminAndExitIfNot() noexcept {
    if (IsRunningAsAdmin()) {
        return true;
    }

    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
        return false;
    }

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.lpVerb = L"runas";
    info.lpFile = path;
    info.hwnd = nullptr;
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info)) {
        MessageBoxW(nullptr, L"Elevation failed.", L"HyprWin", MB_OK | MB_ICONERROR);
    }

    return false;
}

bool ShellOpen(std::wstring_view file, std::wstring_view params) noexcept {
    if (file.empty()) {
        return false;
    }
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", std::wstring(file).c_str(), params.empty() ? nullptr : std::wstring(params).c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
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

bool GetMonitorDpi(HMONITOR handle, UINT& dpiX, UINT& dpiY) noexcept {
    if (!handle) {
        return false;
    }
    return SUCCEEDED(GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY));
}

bool RunProcess(std::wstring_view path, std::wstring_view args, std::wstring_view cwd, bool admin) {
    if (path.empty()) {
        return false;
    }

    if (admin) {
        return RunViaShell(path, args, cwd, L"runas");
    }

    return RunViaExplorerShell(path, args, cwd) || RunAsShellUser(path, args, cwd);
}

bool LaunchApp(std::wstring_view path, std::wstring_view args, std::wstring_view cwd, bool admin) {
    if (path.empty()) {
        return false;
    }

    std::vector<HWND> existing = GetTopLevelWindows();
    ProcessLaunch result = admin ? RunViaShellTracked(path, args, cwd, L"runas") : RunAsShellUserTracked(path, args, cwd);
    if (!result.ok) {
        if (admin || !RunViaExplorerShell(path, args, cwd)) {
            return false;
        }
        return true;
    }

    HWND hwnd = WaitForLaunchWindow(result.pid, result.process, existing, 3000);
    if (hwnd) {
        FocusWindow(hwnd);
    }

    CloseHandleIfValid(result.process);
    return true;
}

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
    DWORD_PTR smResult = 0;
    if (SendMessageTimeoutW(hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&info), SMTO_ABORTIFHUNG | SMTO_NORMAL, 100, &smResult) == 0) {
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
    if (!IsWindow(hwnd))
        return false;
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
    if (!IsWindow(hwnd)) {
        return false;
    }

    return SetWindowPos(hwnd, HWND_TOP, rawRect.left, rawRect.top, rawRect.right - rawRect.left, rawRect.bottom - rawRect.top, SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS) != FALSE;
}

HWND GetFilteredWindowAtPoint(POINT pt) noexcept {
    return InspectWindowAtPoint(pt).candidate;
}

WindowAtPointResult InspectWindowAtPoint(POINT pt) noexcept {
    WindowAtPointResult result{};
    result.hit = WindowFromPoint(pt);
    if (!result.hit || !IsWindow(result.hit)) {
        result.rejection = WindowFilterReason::Invalid;
    } else {
        result.top = GetAncestor(result.hit, GA_ROOT);
        if (!result.top || !IsWindow(result.top)) {
            result.rejection = WindowFilterReason::NoRoot;
        } else if (IsIconic(result.top)) {
            result.rejection = WindowFilterReason::Minimized;
        } else if (IsAppWindow(result.top, &result.rejection)) {
            if (ContainsPointVisual(result.top, pt)) {
                result.candidate = result.top;
                return result;
            }
            result.rejection = WindowFilterReason::OutsideVisualBounds;
        }
    }

    if (result.rejection == WindowFilterReason::ShellProtected) {
        return result;
    }

    HitTestContext context{.pt = pt};
    EnumWindows(&FindWindowAtPointProc, reinterpret_cast<LPARAM>(&context));
    result.candidate = context.result;
    return result;
}

HWND GetFilteredWindowAtCursor() noexcept {
    POINT pt{};
    if (GetCursorPos(&pt) == FALSE) {
        return nullptr;
    }
    return GetFilteredWindowAtPoint(pt);
}

bool PostCloseWindow(HWND hwnd) noexcept {
    if (!IsWindow(hwnd)) {
        return false;
    }
    return PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE;
}

bool IsWindowResponsive(HWND hwnd, DWORD timeoutMs) noexcept {
    DWORD_PTR result = 0;
    return SendMessageTimeout(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, timeoutMs, &result) != 0;
}

// blocks: ShowWindow sends WM_SIZE/WM_SHOWWINDOW to target message pump
bool MinimizeWindow(HWND hwnd) noexcept {
    if (!IsWindow(hwnd))
        return false;
    if (!IsWindowResponsive(hwnd)) {
        LOG_WARN("win: MinimizeWindow: {:p} not responding", reinterpret_cast<void*>(hwnd));
        return false;
    }
    return ShowWindow(hwnd, SW_MINIMIZE) != FALSE;
}

// blocks: ShowWindow sends WM_SIZE/WM_SHOWWINDOW to target message pump
bool MaximizeWindow(HWND hwnd) noexcept {
    if (!IsWindow(hwnd))
        return false;
    if (!IsWindowResponsive(hwnd)) {
        LOG_WARN("win: MaximizeWindow: {:p} not responding", reinterpret_cast<void*>(hwnd));
        return false;
    }
    return ShowWindow(hwnd, SW_MAXIMIZE) != FALSE;
}

// blocks: ShowWindow sends WM_SIZE/WM_SHOWWINDOW to target message pump
bool RestoreWindow(HWND hwnd) noexcept {
    if (!IsWindow(hwnd))
        return false;
    if (!IsWindowResponsive(hwnd)) {
        LOG_WARN("win: RestoreWindow: {:p} not responding", reinterpret_cast<void*>(hwnd));
        return false;
    }
    return ShowWindow(hwnd, SW_RESTORE) != FALSE;
}

DWORD GetProcessId(HWND hwnd) noexcept {
    DWORD pid = 0;
    if (!IsWindow(hwnd)) {
        return 0;
    }

    GetWindowThreadProcessId(hwnd, &pid);
    return pid;
}

std::wstring GetProcessNameByPid(DWORD pid) {
    if (pid == 0) {
        return {};
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return {};
    }

    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    std::wstring name;
    if (QueryFullProcessImageNameW(process, 0, path, &size) != FALSE) {
        const wchar_t* file = wcsrchr(path, L'\\');
        name = file ? file + 1 : path;
    }
    CloseHandle(process);
    return name;
}

std::wstring GetProcessName(HWND hwnd) {
    return GetProcessNameByPid(GetProcessId(hwnd));
}

bool KillWindowProcess(HWND hwnd) noexcept {
    const DWORD pid = GetProcessId(hwnd);
    if (pid == 0) {
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!process) {
        return false;
    }

    const bool ok = TerminateProcess(process, 1) != FALSE;
    CloseHandle(process);
    return ok;
}

// blocks: WM_GETTEXTLENGTH + WM_GETTEXT both send to target message pump
std::wstring GetWindowTitle(HWND hwnd) {
    if (!IsWindow(hwnd))
        return {};
    wchar_t buf[512]{};
    DWORD_PTR result = 0;
    SendMessageTimeout(hwnd, WM_GETTEXT, std::size(buf), reinterpret_cast<LPARAM>(buf), SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result);
    return result > 0 ? std::wstring(buf, result) : std::wstring{};
}

std::wstring GetWindowClass(HWND hwnd) {
    if (!IsWindow(hwnd))
        return {};
    wchar_t buf[256]{};
    const int len = GetClassNameW(hwnd, buf, static_cast<int>(std::size(buf)));
    return len > 0 ? std::wstring(buf, static_cast<std::size_t>(len)) : std::wstring{};
}

std::vector<HWND> GetTopLevelWindows() {
    std::vector<HWND> result;
    EnumWindows(
      [](HWND hwnd, LPARAM lp) -> BOOL {
          if (IsAppWindow(hwnd)) {
              reinterpret_cast<std::vector<HWND>*>(lp)->push_back(hwnd);
          }
          return TRUE;
      },
      reinterpret_cast<LPARAM>(&result));
    return result;
}

namespace {
    MonitorInfo MonitorInfoFromHandle(HMONITOR hmon) noexcept {
        MonitorInfo mi{};
        mi.handle = hmon;
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(hmon, reinterpret_cast<MONITORINFO*>(&info)) != FALSE) {
            mi.rect = info.rcMonitor;
            mi.work_area = info.rcWork;
            mi.is_primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            wcsncpy_s(mi.name, info.szDevice, _TRUNCATE);
        }
        return mi;
    }
} // namespace

std::vector<MonitorInfo> GetMonitors() {
    std::vector<MonitorInfo> result;
    EnumDisplayMonitors(
      nullptr,
      nullptr,
      [](HMONITOR hmon, HDC, LPRECT, LPARAM lp) -> BOOL {
          reinterpret_cast<std::vector<MonitorInfo>*>(lp)->push_back(MonitorInfoFromHandle(hmon));
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

void FocusWindow(HWND hwnd) noexcept {
    if (!IsWindow(hwnd)) {
        return;
    }

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    static_cast<void>(SendInput(1, &input, sizeof(input)));

    const HWND fg = GetForegroundWindow();
    const DWORD foreground_thread = GetWindowThreadProcessId(fg, nullptr);
    const DWORD target_thread = GetWindowThreadProcessId(hwnd, nullptr);
    const DWORD current_thread = GetCurrentThreadId();

    const bool attach_foreground = foreground_thread != 0 && foreground_thread != current_thread && !IsHungAppWindow(fg);
    const bool attach_target = target_thread != 0 && target_thread != current_thread && target_thread != foreground_thread && !IsHungAppWindow(hwnd);

    if (attach_foreground) {
        AttachThreadInput(current_thread, foreground_thread, TRUE);
    }
    if (attach_target) {
        AttachThreadInput(current_thread, target_thread, TRUE);
    }

    AllowSetForegroundWindow(ASFW_ANY);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    if (attach_target) {
        AttachThreadInput(current_thread, target_thread, FALSE);
    }
    if (attach_foreground) {
        AttachThreadInput(current_thread, foreground_thread, FALSE);
    }
}
} // namespace win
