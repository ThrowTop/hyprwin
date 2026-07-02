#include "lua/binds/key_parse.hpp"

#include "util/key_parse.hpp"
#include "util/strings.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>

namespace lua {
namespace {

    enum GenericModifier : std::uint8_t {
        GenericShift = 1u << 0,
        GenericCtrl = 1u << 1,
        GenericAlt = 1u << 2,
    };

    struct ParsedKey {
        UINT vk = 0;
        std::uint8_t concrete = 0;
        std::uint8_t generic = 0;
    };

    void ExpandGeneric(const ParsedKey& parsed, std::size_t index, std::uint8_t modifiers, std::vector<KeyEvent>& out) {
        struct GenEntry {
            std::uint8_t flag, lo, hi;
        };
        static constexpr GenEntry kGeneric[] = {
          {GenericShift, ModLShift, ModRShift},
          {GenericCtrl, ModLCtrl, ModRCtrl},
          {GenericAlt, ModLAlt, ModRAlt},
        };

        while (index < 3 && (parsed.generic & kGeneric[index].flag) == 0)
            ++index;

        if (index == 3) {
            out.push_back(KeyEvent{.vk = parsed.vk, .modifiers = static_cast<std::uint8_t>(parsed.concrete | modifiers)});
            return;
        }

        for (const std::uint8_t bit : {kGeneric[index].lo, kGeneric[index].hi})
            ExpandGeneric(parsed, index + 1, static_cast<std::uint8_t>(modifiers | bit), out);
    }

    bool ApplyToken(ParsedKey& parsed, std::string_view token, std::string& error) {
        if (token == "SHIFT") {
            parsed.generic |= GenericShift;
            return true;
        }
        if (token == "CTRL" || token == "CONTROL") {
            parsed.generic |= GenericCtrl;
            return true;
        }
        if (token == "ALT") {
            parsed.generic |= GenericAlt;
            return true;
        }
        if (token == "LSHIFT") {
            parsed.concrete |= ModLShift;
            return true;
        }
        if (token == "RSHIFT") {
            parsed.concrete |= ModRShift;
            return true;
        }
        if (token == "LCTRL" || token == "LCONTROL") {
            parsed.concrete |= ModLCtrl;
            return true;
        }
        if (token == "RCTRL" || token == "RCONTROL") {
            parsed.concrete |= ModRCtrl;
            return true;
        }
        if (token == "LALT") {
            parsed.concrete |= ModLAlt;
            return true;
        }
        if (token == "RALT") {
            parsed.concrete |= ModRAlt;
            return true;
        }

        if (const UINT vk = ::util::ParseVirtualKey(token)) {
            if (parsed.vk != 0) {
                error = "binding contains more than one non-modifier key";
                return false;
            }
            parsed.vk = vk;
            return true;
        }

        error = "unknown key token: " + std::string(token);
        return false;
    }

} // namespace

bool ParseKeyBinding(std::string_view text, std::vector<KeyEvent>& out, std::string& error) {
    out.clear();
    error.clear();

    ParsedKey parsed;
    while (true) {
        const std::size_t plus = text.find('+');
        const std::string token = ::util::UpperTrim(text.substr(0, plus));
        if (token.empty()) {
            error = "empty key token";
            return false;
        }
        if (!ApplyToken(parsed, token, error))
            return false;
        if (plus == std::string_view::npos)
            break;
        text.remove_prefix(plus + 1);
    }

    if (parsed.vk == 0) {
        error = "binding must include a non-modifier key";
        return false;
    }

    ExpandGeneric(parsed, 0, 0, out);
    std::ranges::sort(out, [](const KeyEvent& l, const KeyEvent& r) { return l.vk < r.vk || (l.vk == r.vk && l.modifiers < r.modifiers); });
    out.erase(std::ranges::unique(out).begin(), out.end());
    return true;
}

UINT ParseSuperKey(std::string_view text) noexcept {
    std::string key;
    for (char c : text)
        key += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (key == "LWIN")
        return VK_LWIN;
    if (key == "RWIN")
        return VK_RWIN;
    return 0;
}

std::string FormatKeyEvent(const KeyEvent& event) {
    std::string s;
    if (event.modifiers & ModLShift)
        s += "LSHIFT+";
    if (event.modifiers & ModRShift)
        s += "RSHIFT+";
    if (event.modifiers & ModLCtrl)
        s += "LCTRL+";
    if (event.modifiers & ModRCtrl)
        s += "RCTRL+";
    if (event.modifiers & ModLAlt)
        s += "LALT+";
    if (event.modifiers & ModRAlt)
        s += "RALT+";
    if ((event.vk >= 'A' && event.vk <= 'Z') || (event.vk >= '0' && event.vk <= '9')) {
        s += static_cast<char>(event.vk);
    } else if (event.vk >= VK_F1 && event.vk <= VK_F24) {
        s += std::format("F{}", event.vk - VK_F1 + 1);
    } else {
        switch (event.vk) {
            case VK_BACK:
                s += "BACKSPACE";
                break;
            case VK_TAB:
                s += "TAB";
                break;
            case VK_RETURN:
                s += "RETURN";
                break;
            case VK_ESCAPE:
                s += "ESC";
                break;
            case VK_SPACE:
                s += "SPACE";
                break;
            case VK_PRIOR:
                s += "PAGEUP";
                break;
            case VK_NEXT:
                s += "PAGEDOWN";
                break;
            case VK_END:
                s += "END";
                break;
            case VK_HOME:
                s += "HOME";
                break;
            case VK_LEFT:
                s += "LEFT";
                break;
            case VK_UP:
                s += "UP";
                break;
            case VK_RIGHT:
                s += "RIGHT";
                break;
            case VK_DOWN:
                s += "DOWN";
                break;
            case VK_INSERT:
                s += "INSERT";
                break;
            case VK_DELETE:
                s += "DELETE";
                break;
            case VK_CAPITAL:
                s += "CAPSLOCK";
                break;
            case VK_NUMLOCK:
                s += "NUMLOCK";
                break;
            case VK_SCROLL:
                s += "SCROLLLOCK";
                break;
            default:
                s += std::format("VK_{:02X}", event.vk);
                break;
        }
    }
    return s;
}

} // namespace lua
