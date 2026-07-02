#include "log/log.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "res/resource_ids.h"
#include "win/native.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <richedit.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windows.h>
#include <windowsx.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef MSFTEDIT_CLASS
#define MSFTEDIT_CLASS L"RICHEDIT50W"
#endif

namespace logging::detail {
namespace {

    constexpr UINT kAppendMessage = WM_APP + 0x510;
    constexpr UINT kCloseMessage = WM_APP + 0x512;
    constexpr COLORREF kBackground = RGB(18, 18, 18);
    constexpr COLORREF kPanel = RGB(28, 28, 28);
    constexpr COLORREF kButtonFill = RGB(31, 31, 31);
    constexpr COLORREF kButtonFillHot = RGB(40, 40, 40);
    constexpr COLORREF kButtonFillChecked = RGB(44, 44, 44);
    constexpr COLORREF kText = RGB(232, 232, 232);
    constexpr COLORREF kMuted = RGB(140, 140, 140);
    constexpr COLORREF kNeutralBorder = RGB(86, 86, 86);
    constexpr COLORREF kSearchBorder = RGB(72, 72, 72);

    // Toolbar geometry
    constexpr int kToolbarHeight = 48;
    constexpr int kBottomBarHeight = 48;
    constexpr int kToolbarPadX = 10;
    constexpr int kButtonY = 9;
    constexpr int kButtonH = 28;
    constexpr int kButtonGap = 6;
    constexpr int kLogInsetX = 10;
    constexpr int kLogGap = 4;

    // Button widths
    constexpr int kLevelW = 64;
    constexpr int kCritW = 58;
    constexpr int kFollowW = 92;
    constexpr int kPauseW = 70;
    constexpr int kFuzzyW = 70;
    constexpr int kClearW = 62;
    constexpr int kCopyW = 56;
    constexpr int kOpenLogW = 88;
    constexpr int kCountW = 90;

    // Search frame geometry
    constexpr int kFramePadX = 3;
    constexpr int kFramePadY = 4;
    constexpr int kSearchInsetX = 11;
    constexpr int kSearchFrameGap = 10;
    constexpr int kSearchMinW = 40;

    // Window constraints
    constexpr int kMinWindowW = 640;
    constexpr int kMinWindowH = 300;
    constexpr int kInitWindowW = 1100;
    constexpr int kInitWindowH = 720;

    // Log buffer
    constexpr int kMaxMessages = 999;
    constexpr int kTrimCount = 200;

    enum ControlId : int {
        IdTrace = 100,
        IdDebug,
        IdInfo,
        IdWarn,
        IdError,
        IdCritical,
        IdSearch,
        IdClear,
        IdCopy,
        IdOpenFolder,
        IdPause,
        IdAutoscroll,
        IdFuzzy,
        IdCount,
    };

    struct ButtonStyle {
        COLORREF accent = kNeutralBorder;
        bool toggle = false;
    };

    COLORREF color_for_level(Level level) {
        switch (level) {
            case Level::Trace:
                return RGB(130, 130, 130);
            case Level::Debug:
                return RGB(90, 190, 255);
            case Level::Info:
                return RGB(220, 235, 220);
            case Level::Warn:
                return RGB(255, 210, 95);
            case Level::Error:
                return RGB(255, 105, 105);
            case Level::Critical:
                return RGB(255, 105, 220);
            case Level::Off:
                return kText;
        }
        return kText;
    }

    std::wstring utf8_to_wide_lossy(std::string_view text) {
        if (text.empty()) {
            return {};
        }

        const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (needed > 0) {
            std::wstring out(static_cast<std::size_t>(needed), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), needed);
            return out;
        }

        std::wstring out;
        out.reserve(text.size());
        for (const unsigned char ch : text) {
            out.push_back(static_cast<wchar_t>(ch));
        }
        return out;
    }

    std::wstring rich_text(std::string_view text) {
        std::string normalized;
        normalized.reserve(text.size() + 16);
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char ch = text[i];
            if (ch == '\n' && (i == 0 || text[i - 1] != '\r')) {
                normalized.push_back('\r');
            }
            normalized.push_back(ch);
        }
        return utf8_to_wide_lossy(normalized);
    }

    std::wstring lower_ascii(std::wstring text) {
        for (wchar_t& ch : text) {
            if (ch >= L'A' && ch <= L'Z') {
                ch = static_cast<wchar_t>(ch - L'A' + L'a');
            }
        }
        return text;
    }

    std::vector<std::size_t> fuzzy_match_positions(std::wstring_view haystack, std::wstring_view needle) {
        std::vector<std::size_t> positions;
        if (needle.empty()) {
            return positions;
        }

        const std::wstring lower_haystack = lower_ascii(std::wstring(haystack));
        const std::wstring lower_needle = lower_ascii(std::wstring(needle));

        std::size_t search_from = 0;
        for (const wchar_t wanted : lower_needle) {
            if (iswspace(wanted)) {
                continue;
            }

            bool found = false;
            for (; search_from < lower_haystack.size(); ++search_from) {
                if (lower_haystack[search_from] == wanted) {
                    positions.push_back(search_from);
                    ++search_from;
                    found = true;
                    break;
                }
            }
            if (!found) {
                positions.clear();
                return positions;
            }
        }

        return positions;
    }

    bool fuzzy_matches(std::wstring_view haystack, std::wstring_view needle) {
        if (needle.empty()) {
            return true;
        }
        return !fuzzy_match_positions(haystack, needle).empty();
    }

    std::vector<std::size_t> substring_match_positions(std::wstring_view haystack, std::wstring_view needle) {
        std::vector<std::size_t> positions;
        if (needle.empty()) {
            return positions;
        }

        const std::wstring lower_haystack = lower_ascii(std::wstring(haystack));
        const std::wstring lower_needle = lower_ascii(std::wstring(needle));
        std::size_t found = lower_haystack.find(lower_needle);
        while (found != std::wstring::npos) {
            for (std::size_t i = 0; i < lower_needle.size(); ++i) {
                positions.push_back(found + i);
            }
            found = lower_haystack.find(lower_needle, found + 1);
        }
        return positions;
    }

    std::vector<std::size_t> match_positions(std::wstring_view haystack, std::wstring_view needle, bool fuzzy) {
        return fuzzy ? fuzzy_match_positions(haystack, needle) : substring_match_positions(haystack, needle);
    }

    struct ViewerState {
        HWND hwnd = nullptr;
        HWND edit = nullptr;
        HWND search = nullptr;
        HWND clear = nullptr;
        HWND copy = nullptr;
        HWND open_folder = nullptr;
        HWND pause = nullptr;
        HWND autoscroll = nullptr;
        HWND fuzzy = nullptr;
        HWND count = nullptr;
        std::array<HWND, 6> levels{};
        HFONT font = nullptr;
        HBRUSH background = nullptr;
        HBRUSH panel = nullptr;
        std::vector<RenderedMessage> messages;
        std::string log_file_path;
        std::array<bool, 6> level_enabled{true, true, true, true, true, true};
        std::wstring search_text;
        std::uint64_t cleared_through = 0;
        std::size_t visible_count = 0;
        bool fuzzy_enabled = true;
        bool paused = false;
        bool autoscroll_enabled = true;
        std::function<void()> on_closed;
    };

    LRESULT CALLBACK search_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR) {
        if (msg == WM_KEYDOWN && wparam == VK_BACK && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            DWORD start = 0;
            DWORD end = 0;
            SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            if (start != end) {
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
                return 0;
            }

            const int len = GetWindowTextLengthW(hwnd);
            std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
            GetWindowTextW(hwnd, text.data(), len + 1);
            text.resize(static_cast<std::size_t>(len));

            std::size_t pos = std::min<std::size_t>(start, text.size());
            while (pos > 0 && iswspace(text[pos - 1])) {
                --pos;
            }
            while (pos > 0 && !iswspace(text[pos - 1])) {
                --pos;
            }

            SendMessageW(hwnd, EM_SETSEL, pos, start);
            SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
            return 0;
        }
        if (msg == WM_CHAR && (GetKeyState(VK_CONTROL) & 0x8000) != 0 && (wparam == 0x08 || wparam == 0x7F)) {
            return 0;
        }
        if (msg == WM_NCDESTROY) {
            RemoveWindowSubclass(hwnd, search_proc, 1);
        }
        return DefSubclassProc(hwnd, msg, wparam, lparam);
    }

    bool visible(const ViewerState& state, const RenderedMessage& message) {
        const bool is_print = (message.level == Level::Off);
        if (!is_print) {
            if (message.sequence <= state.cleared_through) {
                return false;
            }
            const int idx = static_cast<int>(message.level);
            if (idx < 0 || idx >= 6 || !state.level_enabled[static_cast<std::size_t>(idx)]) {
                return false;
            }
        }
        if (state.search_text.empty()) {
            return true;
        }
        const std::wstring text = rich_text(message.line);
        return state.fuzzy_enabled ? fuzzy_matches(text, state.search_text) : !substring_match_positions(text, state.search_text).empty();
    }

    void set_button_checked(HWND hwnd, bool checked) {
        SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    ButtonStyle button_style(int id) {
        if (id >= IdTrace && id <= IdCritical) {
            return ButtonStyle{.accent = color_for_level(static_cast<Level>(id - IdTrace)), .toggle = true};
        }
        if (id == IdPause) {
            return ButtonStyle{.accent = color_for_level(Level::Warn), .toggle = true};
        }
        if (id == IdAutoscroll || id == IdFuzzy) {
            return ButtonStyle{.accent = color_for_level(Level::Debug), .toggle = true};
        }
        if (id == IdClear || id == IdCopy || id == IdOpenFolder) {
            return ButtonStyle{.accent = kNeutralBorder, .toggle = false};
        }
        return ButtonStyle{};
    }

    bool is_owner_button_id(int id) {
        return (id >= IdTrace && id <= IdCritical) || id == IdClear || id == IdCopy || id == IdOpenFolder || id == IdPause || id == IdAutoscroll || id == IdFuzzy;
    }

    COLORREF dim(COLORREF color, int percent) {
        return RGB(GetRValue(color) * percent / 100, GetGValue(color) * percent / 100, GetBValue(color) * percent / 100);
    }

    COLORREF mix(COLORREF lhs, COLORREF rhs, int rhs_percent) {
        const int lhs_percent = 100 - rhs_percent;
        return RGB((GetRValue(lhs) * lhs_percent + GetRValue(rhs) * rhs_percent) / 100,
          (GetGValue(lhs) * lhs_percent + GetGValue(rhs) * rhs_percent) / 100,
          (GetBValue(lhs) * lhs_percent + GetBValue(rhs) * rhs_percent) / 100);
    }

    bool button_checked(const ViewerState& state, int id) {
        if (id >= IdTrace && id <= IdCritical) {
            return state.level_enabled[static_cast<std::size_t>(id - IdTrace)];
        }
        if (id == IdPause) {
            return state.paused;
        }
        if (id == IdAutoscroll) {
            return state.autoscroll_enabled;
        }
        if (id == IdFuzzy) {
            return state.fuzzy_enabled;
        }
        return false;
    }

    void append_to_edit(HWND edit, const RenderedMessage& message, std::wstring_view search_text, bool fuzzy, bool preserve_view = false) {
        CHARRANGE old_selection{};
        POINT old_scroll{};
        if (preserve_view) {
            SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&old_selection));
            SendMessageW(edit, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&old_scroll));
            SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
        }

        const std::wstring text = rich_text(message.line);
        SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        CHARRANGE insert_range{};
        SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&insert_range));
        const LONG insert_start = insert_range.cpMin;

        CHARFORMAT2W fmt{};
        fmt.cbSize = sizeof(fmt);
        fmt.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
        fmt.crTextColor = color_for_level(message.level);
        fmt.yHeight = 190;
        wcscpy_s(fmt.szFaceName, L"Consolas");
        SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
        SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));

        if (!search_text.empty()) {
            const std::vector<std::size_t> positions = match_positions(text, search_text, fuzzy);
            CHARFORMAT2W highlight{};
            highlight.cbSize = sizeof(highlight);
            highlight.dwMask = CFM_COLOR | CFM_BACKCOLOR;
            highlight.crTextColor = RGB(255, 255, 255);
            highlight.crBackColor = mix(color_for_level(message.level), RGB(255, 255, 255), 24);

            for (const std::size_t pos : positions) {
                const LONG char_start = insert_start + static_cast<LONG>(pos);
                SendMessageW(edit, EM_SETSEL, char_start, char_start + 1);
                SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&highlight));
            }
        }

        if (preserve_view) {
            SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&old_selection));
            SendMessageW(edit, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&old_scroll));
            SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(edit, nullptr, TRUE);
        } else {
            SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        }
    }

    void clear_edit(HWND edit) {
        SetWindowTextW(edit, L"");
    }

    void update_count(ViewerState& state) {
        if (state.count == nullptr) {
            return;
        }
        const std::wstring text = std::format(L"{} / {}", state.visible_count, state.messages.size());
        SetWindowTextW(state.count, text.c_str());
    }

    void rebuild(ViewerState& state) {
        if (state.edit == nullptr) {
            return;
        }
        clear_edit(state.edit);
        state.visible_count = 0;
        SendMessageW(state.edit, WM_SETREDRAW, FALSE, 0);
        for (const RenderedMessage& message : state.messages) {
            if (visible(state, message)) {
                ++state.visible_count;
                append_to_edit(state.edit, message, state.search_text, state.fuzzy_enabled);
            }
        }
        SendMessageW(state.edit, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(state.edit, nullptr, TRUE);
        if (state.autoscroll_enabled) {
            SendMessageW(state.edit, EM_SCROLLCARET, 0, 0);
        }
        update_count(state);
    }

    void append_live(ViewerState& state, RenderedMessage message) {
        state.messages.push_back(std::move(message));
        if (static_cast<int>(state.messages.size()) > kMaxMessages) {
            state.messages.erase(state.messages.begin(), state.messages.begin() + kTrimCount);
            rebuild(state);
            return;
        }
        const RenderedMessage& stored = state.messages.back();
        if (!state.paused && visible(state, stored)) {
            ++state.visible_count;
            append_to_edit(state.edit, stored, state.search_text, state.fuzzy_enabled, !state.autoscroll_enabled);
            if (state.autoscroll_enabled) {
                SendMessageW(state.edit, EM_SCROLLCARET, 0, 0);
            }
            update_count(state);
        }
    }

    void layout(ViewerState& state, int width, int height) {
        // Top row: level filters (left) + Pause / Follow (right)
        const wchar_t* labels[] = {L"TRACE", L"DEBUG", L"INFO", L"WARN", L"ERROR", L"CRIT"};
        int x = kToolbarPadX;
        for (std::size_t i = 0; i < state.levels.size(); ++i) {
            const int w = i == 5 ? kCritW : kLevelW;
            MoveWindow(state.levels[i], x, kButtonY, w, kButtonH, TRUE);
            SetWindowTextW(state.levels[i], labels[i]);
            x += w + kButtonGap;
        }
        int rx = width - kToolbarPadX;
        rx -= kFollowW;
        MoveWindow(state.autoscroll, rx, kButtonY, kFollowW, kButtonH, TRUE);
        rx -= kButtonGap + kPauseW;
        MoveWindow(state.pause, rx, kButtonY, kPauseW, kButtonH, TRUE);

        // Bottom row: Fuzzy (left) + [Search + Count inside frame] + Clear / Copy / Open Log (right)
        const int by = height - kBottomBarHeight + kButtonY;
        MoveWindow(state.fuzzy, kToolbarPadX, by, kFuzzyW, kButtonH, TRUE);
        int brx = width - kToolbarPadX;
        brx -= kOpenLogW;
        MoveWindow(state.open_folder, brx, by, kOpenLogW, kButtonH, TRUE);
        brx -= kButtonGap + kCopyW;
        MoveWindow(state.copy, brx, by, kCopyW, kButtonH, TRUE);
        brx -= kButtonGap + kClearW;
        MoveWindow(state.clear, brx, by, kClearW, kButtonH, TRUE);
        const int count_x = brx - kSearchFrameGap - kFramePadX - kCountW;
        const int search_x = kToolbarPadX + kFuzzyW + kButtonGap + kSearchInsetX;
        const int search_w = std::max(kSearchMinW, count_x - kButtonGap - search_x);
        MoveWindow(state.search, search_x, by + kFramePadY, search_w, kButtonH - kFramePadY * 2, TRUE);
        MoveWindow(state.count, count_x, by + kFramePadY, kCountW, kButtonH - kFramePadY * 2, TRUE);

        // Log edit fills between the two bars
        MoveWindow(
          state.edit, kLogInsetX, kToolbarHeight + kLogGap, std::max(0, width - kLogInsetX * 2), std::max(0, height - kToolbarHeight - kLogGap - kLogGap - kBottomBarHeight), TRUE);
    }

    void draw_search_frame(const ViewerState& state, HDC dc) {
        if (state.search == nullptr || state.count == nullptr) {
            return;
        }

        RECT src{};
        GetWindowRect(state.search, &src);
        RECT crc{};
        GetWindowRect(state.count, &crc);
        POINT tl{src.left, src.top};
        POINT br{crc.right, crc.bottom};
        ScreenToClient(state.hwnd, &tl);
        ScreenToClient(state.hwnd, &br);
        RECT rc{tl.x - kFramePadX, tl.y - kFramePadY, br.x + kFramePadX, br.y + kFramePadY};

        HBRUSH fill_brush = CreateSolidBrush(kPanel);
        HPEN border_pen = CreatePen(PS_SOLID, 1, kSearchBorder);
        HGDIOBJ old_brush = SelectObject(dc, fill_brush);
        HGDIOBJ old_pen = SelectObject(dc, border_pen);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 10, 10);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(border_pen);
        DeleteObject(fill_brush);
    }

    HWND create_button(HWND parent, int id, const wchar_t* text) {
        return CreateWindowExW(
          0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    }

    std::wstring visible_text(const ViewerState& state) {
        std::wstring out;
        for (const RenderedMessage& message : state.messages) {
            if (visible(state, message)) {
                out += rich_text(message.line);
            }
        }
        return out;
    }

    void copy_visible_logs(const ViewerState& state) {
        const std::wstring text = visible_text(state);
        if (text.empty())
            return;

        win::SetClipboardText(text, state.hwnd);
    }

    void open_log_folder(const ViewerState& state) {
        if (state.log_file_path.empty()) {
            return;
        }
        const std::filesystem::path folder = std::filesystem::path(state.log_file_path).parent_path();
        if (!folder.empty()) {
            ShellExecuteW(state.hwnd, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    void draw_button(const ViewerState& state, const DRAWITEMSTRUCT& item) {
        wchar_t text[64]{};
        GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));

        const int id = static_cast<int>(item.CtlID);
        if (!is_owner_button_id(id)) {
            return;
        }
        const ButtonStyle style = button_style(id);
        const bool checked = button_checked(state, id);
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const bool focused = (item.itemState & ODS_FOCUS) != 0;

        RECT rc = item.rcItem;
        HBRUSH erase_brush = CreateSolidBrush(kBackground);
        FillRect(item.hDC, &rc, erase_brush);
        DeleteObject(erase_brush);

        InflateRect(&rc, -1, -1);

        COLORREF fill = checked ? mix(kButtonFillChecked, style.accent, 18) : kButtonFill;
        if (pressed) {
            fill = checked ? mix(kButtonFillChecked, style.accent, 28) : RGB(52, 52, 52);
        } else if (focused) {
            fill = checked ? mix(kButtonFillHot, style.accent, 22) : kButtonFillHot;
        }

        const COLORREF border = checked ? mix(style.accent, RGB(255, 255, 255), 18) : dim(style.accent, 58);
        const COLORREF label = checked ? RGB(248, 248, 248) : RGB(166, 166, 166);

        HBRUSH fill_brush = CreateSolidBrush(fill);
        HPEN border_pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ old_brush = SelectObject(item.hDC, fill_brush);
        HGDIOBJ old_pen = SelectObject(item.hDC, border_pen);
        RoundRect(item.hDC, rc.left, rc.top, rc.right, rc.bottom, 10, 10);
        SelectObject(item.hDC, old_pen);
        SelectObject(item.hDC, old_brush);
        DeleteObject(border_pen);
        DeleteObject(fill_brush);

        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, label);
        SelectObject(item.hDC, state.font);
        DrawTextW(item.hDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        auto* state = reinterpret_cast<ViewerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg) {
            case WM_GETMINMAXINFO: {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
                mmi->ptMinTrackSize = {kMinWindowW, kMinWindowH};
                return 0;
            }
            case WM_NCCREATE: {
                const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
                state = reinterpret_cast<ViewerState*>(create->lpCreateParams);
                state->hwnd = hwnd;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
                return TRUE;
            }
            case WM_CREATE: {
                const BOOL dark = TRUE;
                DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
                state->background = CreateSolidBrush(kBackground);
                state->panel = CreateSolidBrush(kPanel);
                state->font = CreateFontW(-14,
                  0,
                  0,
                  0,
                  FW_NORMAL,
                  FALSE,
                  FALSE,
                  FALSE,
                  DEFAULT_CHARSET,
                  OUT_DEFAULT_PRECIS,
                  CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY,
                  DEFAULT_PITCH | FF_DONTCARE,
                  L"Segoe UI");

                LoadLibraryW(L"Msftedit.dll");
                state->edit = CreateWindowExW(0,
                  MSFTEDIT_CLASS,
                  L"",
                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
                  0,
                  0,
                  0,
                  0,
                  hwnd,
                  nullptr,
                  GetModuleHandleW(nullptr),
                  nullptr);
                SetWindowTheme(state->edit, L"DarkMode_Explorer", nullptr);
                SendMessageW(state->edit, EM_SETBKGNDCOLOR, 0, kBackground);
                SendMessageW(state->edit, EM_EXLIMITTEXT, 0, 8 * 1024 * 1024);
                SendMessageW(state->edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(12, 12));

                for (std::size_t i = 0; i < state->levels.size(); ++i) {
                    state->levels[i] = create_button(hwnd, IdTrace + static_cast<int>(i), L"");
                    set_button_checked(state->levels[i], true);
                }
                state->search =
                  CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IdSearch), GetModuleHandleW(nullptr), nullptr);
                SetWindowTheme(state->search, L"DarkMode_Explorer", nullptr);
                SendMessageW(state->search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search"));
                SendMessageW(state->search, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
                SetWindowSubclass(state->search, search_proc, 1, 0);
                state->clear = create_button(hwnd, IdClear, L"Clear");
                state->copy = create_button(hwnd, IdCopy, L"Copy");
                state->open_folder = create_button(hwnd, IdOpenFolder, L"Open Log");
                state->pause = create_button(hwnd, IdPause, L"Pause");
                state->autoscroll = create_button(hwnd, IdAutoscroll, L"Follow");
                state->fuzzy = create_button(hwnd, IdFuzzy, L"Fuzzy");
                state->count = CreateWindowExW(
                  0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IdCount), GetModuleHandleW(nullptr), nullptr);
                set_button_checked(state->autoscroll, true);

                for (HWND child : state->levels) {
                    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                }
                for (HWND child : {state->search, state->clear, state->copy, state->open_folder, state->pause, state->autoscroll, state->fuzzy, state->count}) {
                    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                }
                rebuild(*state);
                return 0;
            }
            case WM_SIZE:
                if (state != nullptr) {
                    layout(*state, LOWORD(lparam), HIWORD(lparam));
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
                return 0;
            case WM_ERASEBKGND: {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                FillRect(reinterpret_cast<HDC>(wparam), &rc, state != nullptr ? state->background : reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                if (state != nullptr) {
                    draw_search_frame(*state, reinterpret_cast<HDC>(wparam));
                }
                return 1;
            }
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC dc = BeginPaint(hwnd, &ps);
                if (state != nullptr) {
                    draw_search_frame(*state, dc);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORSTATIC:
                if (state != nullptr && reinterpret_cast<HWND>(lparam) == state->search) {
                    SetTextColor(reinterpret_cast<HDC>(wparam), kText);
                    SetBkColor(reinterpret_cast<HDC>(wparam), kPanel);
                    return reinterpret_cast<LRESULT>(state->panel);
                }
                if (state != nullptr && reinterpret_cast<HWND>(lparam) == state->count) {
                    SetTextColor(reinterpret_cast<HDC>(wparam), kMuted);
                    SetBkColor(reinterpret_cast<HDC>(wparam), kPanel);
                    return reinterpret_cast<LRESULT>(state->panel);
                }
                break;
            case WM_COMMAND:
                if (state != nullptr) {
                    const int id = LOWORD(wparam);
                    const int notify = HIWORD(wparam);
                    if (id >= IdTrace && id <= IdCritical) {
                        const std::size_t idx = static_cast<std::size_t>(id - IdTrace);
                        state->level_enabled[idx] = !state->level_enabled[idx];
                        rebuild(*state);
                        InvalidateRect(state->levels[idx], nullptr, TRUE);
                        return 0;
                    }
                    if (id == IdClear) {
                        state->cleared_through = state->messages.empty() ? state->cleared_through : state->messages.back().sequence;
                        clear_edit(state->edit);
                        state->visible_count = 0;
                        update_count(*state);
                        return 0;
                    }
                    if (id == IdCopy) {
                        copy_visible_logs(*state);
                        return 0;
                    }
                    if (id == IdOpenFolder) {
                        open_log_folder(*state);
                        return 0;
                    }
                    if (id == IdPause) {
                        state->paused = !state->paused;
                        if (!state->paused) {
                            rebuild(*state);
                        }
                        InvalidateRect(state->pause, nullptr, TRUE);
                        return 0;
                    }
                    if (id == IdAutoscroll) {
                        state->autoscroll_enabled = !state->autoscroll_enabled;
                        InvalidateRect(state->autoscroll, nullptr, TRUE);
                        return 0;
                    }
                    if (id == IdFuzzy) {
                        state->fuzzy_enabled = !state->fuzzy_enabled;
                        rebuild(*state);
                        InvalidateRect(state->fuzzy, nullptr, TRUE);
                        return 0;
                    }
                    if (id == IdSearch && notify == EN_CHANGE) {
                        const int len = GetWindowTextLengthW(state->search);
                        std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
                        GetWindowTextW(state->search, text.data(), len + 1);
                        text.resize(static_cast<std::size_t>(len));
                        state->search_text = lower_ascii(text);
                        rebuild(*state);
                        return 0;
                    }
                }
                break;
            case WM_DRAWITEM:
                if (state != nullptr) {
                    const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
                    if (item != nullptr && item->CtlType == ODT_BUTTON && is_owner_button_id(static_cast<int>(item->CtlID))) {
                        draw_button(*state, *item);
                        return TRUE;
                    }
                }
                break;
            case kAppendMessage: {
                std::unique_ptr<RenderedMessage> message(reinterpret_cast<RenderedMessage*>(lparam));
                if (state != nullptr && message) {
                    append_live(*state, std::move(*message));
                }
                return 0;
            }
            case kCloseMessage:
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_NCDESTROY:
                if (state != nullptr) {
                    if (state->on_closed) {
                        state->on_closed();
                    }
                    if (state->font != nullptr)
                        DeleteObject(state->font);
                    if (state->background != nullptr)
                        DeleteObject(state->background);
                    if (state->panel != nullptr)
                        DeleteObject(state->panel);
                    delete state;
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                }
                PostQuitMessage(0);
                return 0;
        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    bool register_class() {
        static const bool registered = [] {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = window_proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APPICON));
            wc.hIconSm = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
            wc.hbrBackground = CreateSolidBrush(kBackground);
            wc.lpszClassName = L"LoggingRichViewer";
            return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }();
        return registered;
    }

} // namespace

struct LogViewer::Impl {
    std::jthread thread;
    std::atomic<bool> active{false};
    std::atomic<HWND> hwnd{nullptr};
    std::mutex mutex;
    std::vector<RenderedMessage> pending;
};

LogViewer::~LogViewer() {
    close();
}

bool LogViewer::open(std::vector<RenderedMessage> initial, std::string log_file_path, std::function<void()> on_closed) {
    if (is_open()) {
        return true;
    }
    if (m_impl != nullptr && m_impl->thread.joinable()) {
        m_impl->thread.detach();
    }
    m_impl = std::make_shared<Impl>();

    m_impl->active.store(true, std::memory_order_release);

    m_impl->thread =
      std::jthread([impl = m_impl, initial = std::move(initial), log_file_path = std::move(log_file_path), on_closed = std::move(on_closed)](std::stop_token token) mutable {
          if (!register_class()) {
              impl->active.store(false, std::memory_order_release);
              if (on_closed)
                  on_closed();
              return;
          }

          auto* state = new ViewerState();
          state->messages = std::move(initial);
          state->log_file_path = std::move(log_file_path);
          state->on_closed = std::move(on_closed);
          {
              std::lock_guard lock(impl->mutex);
              state->messages.insert(state->messages.end(), std::make_move_iterator(impl->pending.begin()), std::make_move_iterator(impl->pending.end()));
              impl->pending.clear();
          }
          HWND hwnd = CreateWindowExW(0,
            L"LoggingRichViewer",
            L"HyprWin Log",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            kInitWindowW,
            kInitWindowH,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            state);
          if (hwnd == nullptr) {
              delete state;
              impl->active.store(false, std::memory_order_release);
              if (on_closed)
                  on_closed();
              return;
          }

          impl->hwnd.store(hwnd, std::memory_order_release);
          {
              std::vector<RenderedMessage> late_pending;
              {
                  std::lock_guard lock(impl->mutex);
                  late_pending = std::move(impl->pending);
                  impl->pending.clear();
              }
              for (RenderedMessage& message : late_pending) {
                  auto* payload = new RenderedMessage(std::move(message));
                  if (!PostMessageW(hwnd, kAppendMessage, 0, reinterpret_cast<LPARAM>(payload))) {
                      delete payload;
                  }
              }
          }

          std::stop_callback close_on_stop(token, [hwnd] { PostMessageW(hwnd, kCloseMessage, 0, 0); });

          ShowWindow(hwnd, SW_SHOWNORMAL);
          UpdateWindow(hwnd);
          SetWindowTextW(hwnd, L"HyprWin Log");
          SetForegroundWindow(hwnd);

          MSG msg{};
          while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
              TranslateMessage(&msg);
              DispatchMessageW(&msg);
          }
          impl->hwnd.store(nullptr, std::memory_order_release);
          impl->active.store(false, std::memory_order_release);
      });

    return true;
}

void LogViewer::close() {
    if (m_impl == nullptr) {
        return;
    }
    m_impl->active.store(false, std::memory_order_release);
    if (HWND hwnd = m_impl->hwnd.exchange(nullptr, std::memory_order_acq_rel)) {
        PostMessageW(hwnd, kCloseMessage, 0, 0);
    }
    if (m_impl->thread.joinable()) {
        m_impl->thread.request_stop();
        m_impl->thread.join();
    }
}

void LogViewer::close_async() {
    if (m_impl == nullptr) {
        return;
    }
    m_impl->active.store(false, std::memory_order_release);
    if (HWND hwnd = m_impl->hwnd.exchange(nullptr, std::memory_order_acq_rel)) {
        PostMessageW(hwnd, kCloseMessage, 0, 0);
    }
}

bool LogViewer::is_open() const noexcept {
    return m_impl != nullptr && m_impl->active.load(std::memory_order_acquire);
}

void LogViewer::append(RenderedMessage message) {
    if (m_impl == nullptr) {
        return;
    }
    if (!m_impl->active.load(std::memory_order_acquire)) {
        return;
    }
    HWND hwnd = m_impl->hwnd.load(std::memory_order_acquire);
    if (hwnd == nullptr) {
        std::lock_guard lock(m_impl->mutex);
        if (m_impl->active.load(std::memory_order_relaxed) && m_impl->hwnd.load(std::memory_order_relaxed) == nullptr) {
            m_impl->pending.push_back(std::move(message));
        }
        return;
    }
    auto* payload = new RenderedMessage(std::move(message));
    if (!PostMessageW(hwnd, kAppendMessage, 0, reinterpret_cast<LPARAM>(payload))) {
        delete payload;
    }
}

} // namespace logging::detail
