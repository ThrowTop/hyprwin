#include "win/ui.hpp"

#include "log/log.hpp"

#include <atomic>
#include <cstring>
#include <thread>

namespace win {

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

} // namespace win
