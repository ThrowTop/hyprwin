#pragma once

#include <cstdint>
#include <memory>
#include <variant>

#include "config/settings.hpp"
#include "overlay/outline/types.hpp"
#include "util/geometry.hpp"

namespace hw {

using InteractionId = std::uint64_t;

enum class SessionType { None, Drag, Resize };

struct DragSession {
    InteractionId interactionId = 0;
    HWND target = nullptr;
    vec::i4 originalRawRect{};
    vec::i2 anchor{};
    vec::i2 windowSize{};
    vec::i4 visualOffset{};
    float dpiScale = 1.0f;
};

struct ResizeSession {
    InteractionId interactionId = 0;
    HWND target = nullptr;
    vec::i4 originalRawRect{};
    vec::i2 startCursor{};
    vec::i4 startRect{};
    ResizeCorner corner = ResizeCorner::BottomRight;
    vec::i2 minSize{};
    vec::i2 maxSize{};
    vec::i4 visualOffset{};
    float dpiScale = 1.0f;
};

struct CommitInteraction {
    InteractionId interactionId = 0;
};
struct CancelInteraction {
    InteractionId interactionId = 0;
};
using UseBuiltInShader = outline::UseBuiltInShader;
using InstallPixelShader = outline::InstallPixelShader;

struct BeginDrag {
    DragSession session{};
    vec::i4 initialBounds{};
};

struct BeginResize {
    ResizeSession session{};
};

using OverlayCmd = std::variant<CommitInteraction, CancelInteraction, UseBuiltInShader, InstallPixelShader, BeginDrag, BeginResize>;

using OverlayActiveSession = std::variant<std::monostate, DragSession, ResizeSession>;

inline SessionType GetSessionType(const OverlayActiveSession& active) noexcept {
    if (std::holds_alternative<DragSession>(active))
        return SessionType::Drag;
    if (std::holds_alternative<ResizeSession>(active))
        return SessionType::Resize;
    return SessionType::None;
}

} // namespace hw
