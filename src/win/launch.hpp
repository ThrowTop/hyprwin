#pragma once

#include <string_view>

namespace win {

bool RunProcess(std::wstring_view path, std::wstring_view args, std::wstring_view cwd = {}, bool admin = false);
bool LaunchApp(std::wstring_view path, std::wstring_view args, std::wstring_view cwd = {}, bool admin = false);

} // namespace win
