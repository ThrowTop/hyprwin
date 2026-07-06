#include "win/launch.hpp"

#include "win/window.hpp"
#include "win/window_internal.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <ole2.h>
#include <oleauto.h>

#include <exdisp.h>
#include <shellapi.h>
#include <shldisp.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <userenv.h>
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

    struct LaunchWindowContext {
        DWORD pid = 0;
        const std::vector<HWND>* existing = nullptr;
        HWND result = nullptr;
    };

    BOOL CALLBACK FindLaunchWindowProc(HWND hwnd, LPARAM param) noexcept {
        auto& context = *reinterpret_cast<LaunchWindowContext*>(param);
        if (!detail::IsAppWindow(hwnd)) {
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
        if (!context || context->result || objectId != OBJID_WINDOW || childId != CHILDID_SELF || !detail::IsAppWindow(hwnd)) {
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
        HWINEVENTHOOK hook =
          SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, LaunchWindowEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

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

            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
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

        const std::wstring cwdCopy{cwd};
        const BOOL ok = CreateProcessWithTokenW(
          primary,
          LOGON_WITH_PROFILE,
          nullptr,
          command.data(),
          CREATE_UNICODE_ENVIRONMENT,
          environment,
          cwdCopy.empty() ? nullptr : cwdCopy.c_str(),
          &startup,
          &info);

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
        const std::wstring pathCopy{path};
        const std::wstring argsCopy{args};
        const std::wstring cwdCopy{cwd};

        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = verb;
        info.lpFile = pathCopy.c_str();
        info.lpParameters = argsCopy.empty() ? nullptr : argsCopy.c_str();
        info.lpDirectory = cwdCopy.empty() ? nullptr : cwdCopy.c_str();
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
        const std::wstring pathCopy{path};
        const std::wstring argsCopy{args};
        const std::wstring cwdCopy{cwd};

        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.lpVerb = verb;
        info.lpFile = pathCopy.c_str();
        info.lpParameters = argsCopy.empty() ? nullptr : argsCopy.c_str();
        info.lpDirectory = cwdCopy.empty() ? nullptr : cwdCopy.c_str();
        info.nShow = SW_SHOWNORMAL;
        return ShellExecuteExW(&info) != FALSE;
    }

    bool GetDesktopAutomationObject(REFIID riid, void** result) {
        *result = nullptr;

        ComPtr<IShellWindows> shellWindows;
        HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(shellWindows.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        VARIANT empty{};
        OleVariant location(static_cast<long>(CSIDL_DESKTOP));

        long hwnd = 0;
        ComPtr<IDispatch> dispatch;
        hr = shellWindows->FindWindowSW(&location.Get(), &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, dispatch.GetAddressOf());
        if (FAILED(hr) || !dispatch) {
            return false;
        }

        ComPtr<IServiceProvider> provider;
        hr = dispatch.As(&provider);
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IShellBrowser> browser;
        hr = provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(browser.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IShellView> view;
        hr = browser->QueryActiveShellView(view.GetAddressOf());
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IDispatch> viewDispatch;
        hr = view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(viewDispatch.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IShellFolderViewDual> folderView;
        hr = viewDispatch.As(&folderView);
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IDispatch> appDispatch;
        hr = folderView->get_Application(appDispatch.GetAddressOf());
        if (FAILED(hr)) {
            return false;
        }
        return SUCCEEDED(appDispatch->QueryInterface(riid, result));
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
        OleVariant argsValue(args);
        OleVariant directoryValue(cwd);
        OleVariant operation(L"open");
        OleVariant show(static_cast<long>(SW_SHOWNORMAL));

        return SUCCEEDED(shell->ShellExecuteW(file.Get(), argsValue.Get(), directoryValue.Get(), operation.Get(), show.Get()));
    }
} // namespace

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

} // namespace win
