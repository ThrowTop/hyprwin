#include "win/app.hpp"

#include <shellapi.h>
#include <shobjidl.h>

namespace win {
namespace {

    bool IsRunningAsAdmin() noexcept {
        BOOL isAdmin = FALSE;
        SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
        PSID adminGroup = nullptr;

        if (AllocateAndInitializeSid(
              &ntAuthority,
              2,
              SECURITY_BUILTIN_DOMAIN_RID,
              DOMAIN_ALIAS_RID_ADMINS,
              0,
              0,
              0,
              0,
              0,
              0,
              &adminGroup)) {
            CheckTokenMembership(nullptr, adminGroup, &isAdmin);
            FreeSid(adminGroup);
        }

        return isAdmin != FALSE;
    }

} // namespace

SingleInstance::SingleInstance(HANDLE mutex) noexcept : m_mutex(mutex), m_error(GetLastError()) {}

SingleInstance::~SingleInstance() {
    if (m_mutex) {
        CloseHandle(m_mutex);
    }
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

bool SetProcessIdentity(const wchar_t* appId) noexcept {
    return SUCCEEDED(SetCurrentProcessExplicitAppUserModelID(appId));
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
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info)) {
        MessageBoxW(nullptr, L"Elevation failed.", L"HyprWin", MB_OK | MB_ICONERROR);
    }
    return false;
}

DWORD GetProcessId(HWND hwnd) noexcept {
    DWORD pid = 0;
    if (IsWindow(hwnd)) {
        GetWindowThreadProcessId(hwnd, &pid);
    }
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

} // namespace win
