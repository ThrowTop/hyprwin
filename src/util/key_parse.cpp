#include "util/key_parse.hpp"

#include "util/strings.hpp"

#include <array>
#include <charconv>
#include <string>

namespace util {
namespace {

    std::optional<UINT> NamedKey(std::string_view token) noexcept {
        struct Entry {
            std::string_view name;
            UINT vk;
        };

        static constexpr std::array entries{
          Entry{"BACKSPACE", VK_BACK},
          Entry{"TAB", VK_TAB},
          Entry{"RETURN", VK_RETURN},
          Entry{"ENTER", VK_RETURN},
          Entry{"ESC", VK_ESCAPE},
          Entry{"ESCAPE", VK_ESCAPE},
          Entry{"SPACE", VK_SPACE},
          Entry{"PAGEUP", VK_PRIOR},
          Entry{"PAGEDOWN", VK_NEXT},
          Entry{"END", VK_END},
          Entry{"HOME", VK_HOME},
          Entry{"LEFT", VK_LEFT},
          Entry{"UP", VK_UP},
          Entry{"RIGHT", VK_RIGHT},
          Entry{"DOWN", VK_DOWN},
          Entry{"INSERT", VK_INSERT},
          Entry{"DELETE", VK_DELETE},
          Entry{"PAUSE", VK_PAUSE},
          Entry{"PRINTSCREEN", VK_SNAPSHOT},
          Entry{"SNAPSHOT", VK_SNAPSHOT},
          Entry{"CAPSLOCK", VK_CAPITAL},
          Entry{"NUMLOCK", VK_NUMLOCK},
          Entry{"SCROLLLOCK", VK_SCROLL},
          Entry{"PERIOD", VK_OEM_PERIOD},
          Entry{"COMMA", VK_OEM_COMMA},
          Entry{"MINUS", VK_OEM_MINUS},
          Entry{"PLUS", VK_OEM_PLUS},
          Entry{"SLASH", VK_OEM_2},
          Entry{"BACKSLASH", VK_OEM_5},
          Entry{"SEMICOLON", VK_OEM_1},
          Entry{"QUOTE", VK_OEM_7},
          Entry{"LBRACKET", VK_OEM_4},
          Entry{"RBRACKET", VK_OEM_6},
          Entry{"NUMPAD0", VK_NUMPAD0},
          Entry{"NUMPAD1", VK_NUMPAD1},
          Entry{"NUMPAD2", VK_NUMPAD2},
          Entry{"NUMPAD3", VK_NUMPAD3},
          Entry{"NUMPAD4", VK_NUMPAD4},
          Entry{"NUMPAD5", VK_NUMPAD5},
          Entry{"NUMPAD6", VK_NUMPAD6},
          Entry{"NUMPAD7", VK_NUMPAD7},
          Entry{"NUMPAD8", VK_NUMPAD8},
          Entry{"NUMPAD9", VK_NUMPAD9},
          Entry{"MULTIPLY", VK_MULTIPLY},
          Entry{"ADD", VK_ADD},
          Entry{"SUBTRACT", VK_SUBTRACT},
          Entry{"DECIMAL", VK_DECIMAL},
          Entry{"DIVIDE", VK_DIVIDE},
        };

        for (const Entry& e : entries) {
            if (token == e.name)
                return e.vk;
        }

        if (token.size() >= 2 && token[0] == 'F') {
            int n = 0;
            const auto r = std::from_chars(token.data() + 1, token.data() + token.size(), n);
            if (r.ec == std::errc{} && r.ptr == token.data() + token.size() && n >= 1 && n <= 24)
                return static_cast<UINT>(VK_F1 + n - 1);
        }

        if (token.size() == 1) {
            const char ch = token[0];
            if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))
                return static_cast<UINT>(ch);
        }

        return std::nullopt;
    }

} // namespace

UINT ParseVirtualKey(std::string_view text) noexcept {
    const std::string key = UpperTrim(text);
    if (const auto vk = NamedKey(key))
        return *vk;
    return 0;
}

} // namespace util
