#pragma once

#include <string>
#include <string_view>

#include <windows.h>

namespace win {

bool SetClipboardText(std::wstring_view text, HWND owner = nullptr) noexcept;
void ShowMessageBoxAsync(std::wstring text, std::wstring title, UINT flags) noexcept;

} // namespace win
