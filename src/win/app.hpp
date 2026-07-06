#pragma once

#include <filesystem>
#include <string>

#include <windows.h>

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

bool SetProcessIdentity(const wchar_t* appId) noexcept;
bool SetPerMonitorDpiAwareness() noexcept;
void DisableProcessThrottling() noexcept;
[[nodiscard]] std::filesystem::path GetModuleDirectory();
bool EnsureRunAsAdminAndExitIfNot() noexcept;

[[nodiscard]] DWORD GetProcessId(HWND hwnd) noexcept;
[[nodiscard]] std::wstring GetProcessNameByPid(DWORD pid);
[[nodiscard]] std::wstring GetProcessName(HWND hwnd);
[[nodiscard]] bool KillWindowProcess(HWND hwnd) noexcept;

} // namespace win
