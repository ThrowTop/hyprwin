#include "overlay/bounds.hpp"

namespace hw {

vec::i4 ComputeDragBounds(vec::i2 cursor, const DragSession& session) noexcept {
    const int x = cursor.x - session.anchor.x;
    const int y = cursor.y - session.anchor.y;
    return vec::i4{x, y, x + session.windowSize.x, y + session.windowSize.y};
}

vec::i4 ComputeResizeBounds(vec::i2 cursor, const ResizeSession& session) noexcept {
    const int dx = cursor.x - session.startCursor.x;
    const int dy = cursor.y - session.startCursor.y;

    vec::i4 bounds = session.startRect;
    switch (session.corner) {
        case ResizeCorner::TopLeft:
            bounds.x += dx;
            bounds.y += dy;
            break;
        case ResizeCorner::TopRight:
            bounds.x2 += dx;
            bounds.y += dy;
            break;
        case ResizeCorner::BottomLeft:
            bounds.x += dx;
            bounds.y2 += dy;
            break;
        case ResizeCorner::BottomRight:
            bounds.x2 += dx;
            bounds.y2 += dy;
            break;
        case ResizeCorner::Closest:
            break;
    }

    return ApplyResizeConstraints(bounds, session.corner, session.minSize, session.maxSize);
}

vec::i4 ApplyResizeConstraints(vec::i4 bounds, ResizeCorner corner, vec::i2 minSize, vec::i2 maxSize) noexcept {
    const bool ownsLeft = corner == ResizeCorner::TopLeft || corner == ResizeCorner::BottomLeft;
    const bool ownsTop = corner == ResizeCorner::TopLeft || corner == ResizeCorner::TopRight;

    if (minSize.x > 0 && bounds.Width() < minSize.x) {
        if (ownsLeft) {
            bounds.x = bounds.x2 - minSize.x;
        } else {
            bounds.x2 = bounds.x + minSize.x;
        }
    }

    if (minSize.y > 0 && bounds.Height() < minSize.y) {
        if (ownsTop) {
            bounds.y = bounds.y2 - minSize.y;
        } else {
            bounds.y2 = bounds.y + minSize.y;
        }
    }

    if (maxSize.x > 0 && bounds.Width() > maxSize.x) {
        if (ownsLeft) {
            bounds.x = bounds.x2 - maxSize.x;
        } else {
            bounds.x2 = bounds.x + maxSize.x;
        }
    }

    if (maxSize.y > 0 && bounds.Height() > maxSize.y) {
        if (ownsTop) {
            bounds.y = bounds.y2 - maxSize.y;
        } else {
            bounds.y2 = bounds.y + maxSize.y;
        }
    }

    return bounds;
}

vec::i4 ApplyVisualOffset(vec::i4 logicalBounds, vec::i4 visualOffset) noexcept {
    return logicalBounds + visualOffset;
}

} // namespace hw
