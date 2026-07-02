#pragma once

#include <cstdint>
#include <memory>
#include <variant>

#include "config/settings.hpp"
#include "shader/types.hpp"
#include "util/geometry.hpp"

namespace hw {

enum class SessionType { None, Drag, Resize };

struct DragSession {
    vec::i2 anchor{};
    vec::i2 windowSize{};
    vec::i4 visualOffset{};
};

struct ResizeSession {
    vec::i2 startCursor{};
    vec::i4 startRect{};
    ResizeCorner corner = ResizeCorner::BottomRight;
    vec::i2 minSize{};
    vec::i2 maxSize{};
    vec::i4 visualOffset{};
};

struct Hide {};
struct ResetDevice {};
struct Shutdown {};
struct UseBuiltInShader {
    std::uint64_t generation = 0;
};
struct InstallPixelShader {
    std::shared_ptr<const shader::Bytecode> bytecode;
    std::uint64_t generation = 0;
};

struct BeginDrag {
    DragSession session{};
    vec::i4 initialBounds{};
};

struct BeginResize {
    ResizeSession session{};
};

using OverlayCmd = std::variant<Hide, ResetDevice, Shutdown, UseBuiltInShader, InstallPixelShader, BeginDrag, BeginResize>;

using OverlayActiveSession = std::variant<std::monostate, DragSession, ResizeSession>;

inline SessionType GetSessionType(const OverlayActiveSession& active) noexcept {
    if (std::holds_alternative<DragSession>(active))
        return SessionType::Drag;
    if (std::holds_alternative<ResizeSession>(active))
        return SessionType::Resize;
    return SessionType::None;
}

} // namespace hw
