#include "overlay/session.hpp"

#include "overlay/bounds.hpp"

namespace hw::session {

bool IsActive(const OverlayActiveSession& active) noexcept {
    return !std::holds_alternative<std::monostate>(active);
}

vec::i4 InitialBounds(const DragSession& session, const BeginDrag& cmd) noexcept {
    if (cmd.initialBounds.x2 != cmd.initialBounds.x || cmd.initialBounds.y2 != cmd.initialBounds.y) {
        return cmd.initialBounds;
    }

    return vec::i4{0, 0, session.windowSize.x, session.windowSize.y};
}

vec::i4 ComputeBounds(const OverlayActiveSession& active, POINT cursor) noexcept {
    const vec::i2 cur{cursor.x, cursor.y};
    if (const auto* drag = std::get_if<DragSession>(&active)) {
        return ComputeDragBounds(cur, *drag);
    }

    if (const auto* resize = std::get_if<ResizeSession>(&active)) {
        return ComputeResizeBounds(cur, *resize);
    }

    return {};
}

vec::i4 VisualOffset(const OverlayActiveSession& active) noexcept {
    if (const auto* drag = std::get_if<DragSession>(&active)) {
        return drag->visualOffset;
    }

    if (const auto* resize = std::get_if<ResizeSession>(&active)) {
        return resize->visualOffset;
    }

    return {};
}

float DpiScale(const OverlayActiveSession& active) noexcept {
    if (const auto* drag = std::get_if<DragSession>(&active)) {
        return drag->dpiScale;
    }

    if (const auto* resize = std::get_if<ResizeSession>(&active)) {
        return resize->dpiScale;
    }

    return 1.0f;
}

HWND Target(const OverlayActiveSession& active) noexcept {
    if (const auto* drag = std::get_if<DragSession>(&active)) {
        return drag->target;
    }
    if (const auto* resize = std::get_if<ResizeSession>(&active)) {
        return resize->target;
    }
    return nullptr;
}

InteractionId Id(const OverlayActiveSession& active) noexcept {
    if (const auto* drag = std::get_if<DragSession>(&active)) {
        return drag->interactionId;
    }
    if (const auto* resize = std::get_if<ResizeSession>(&active)) {
        return resize->interactionId;
    }
    return 0;
}

vec::i4 OriginalRawRect(const OverlayActiveSession& active) noexcept {
    if (const auto* drag = std::get_if<DragSession>(&active)) {
        return drag->originalRawRect;
    }
    if (const auto* resize = std::get_if<ResizeSession>(&active)) {
        return resize->originalRawRect;
    }
    return {};
}

const char* TypeName(SessionType type) noexcept {
    switch (type) {
        case SessionType::None:
            return "none";
        case SessionType::Drag:
            return "drag";
        case SessionType::Resize:
            return "resize";
    }
    return "unknown";
}

void SetCursor(const OverlayActiveSession& active) noexcept {
    static HCURSOR move = LoadCursorW(nullptr, IDC_SIZEALL);
    static HCURSOR resizeNwse = LoadCursorW(nullptr, IDC_SIZENWSE);
    static HCURSOR resizeNesw = LoadCursorW(nullptr, IDC_SIZENESW);
    static HCURSOR arrow = LoadCursorW(nullptr, IDC_ARROW);

    if (std::holds_alternative<DragSession>(active)) {
        ::SetCursor(move);
        return;
    }

    if (const auto* resize = std::get_if<ResizeSession>(&active)) {
        switch (resize->corner) {
            case ResizeCorner::TopLeft:
            case ResizeCorner::BottomRight:
                ::SetCursor(resizeNwse);
                return;
            case ResizeCorner::TopRight:
            case ResizeCorner::BottomLeft:
                ::SetCursor(resizeNesw);
                return;
            case ResizeCorner::Closest:
                break;
        }
    }

    ::SetCursor(arrow);
}

} // namespace hw::session
