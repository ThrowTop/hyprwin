#pragma once

#include "overlay/cmd.hpp"
#include "util/geometry.hpp"

namespace hw {

[[nodiscard]] vec::i4 ComputeDragBounds(vec::i2 cursor, const DragSession& session) noexcept;
[[nodiscard]] vec::i4 ComputeResizeBounds(vec::i2 cursor, const ResizeSession& session) noexcept;
[[nodiscard]] vec::i4 ApplyResizeConstraints(vec::i4 bounds, ResizeCorner corner, vec::i2 minSize, vec::i2 maxSize) noexcept;
[[nodiscard]] vec::i4 ApplyVisualOffset(vec::i4 logicalBounds, vec::i4 visualOffset) noexcept;

} // namespace hw
