#pragma once

#include <windows.h>

#include "overlay/cmd.hpp"

namespace hw::session {

[[nodiscard]] bool IsActive(const OverlayActiveSession& active) noexcept;
[[nodiscard]] vec::i4 InitialBounds(const DragSession& session, const BeginDrag& cmd) noexcept;
[[nodiscard]] vec::i4 ComputeBounds(const OverlayActiveSession& active, POINT cursor) noexcept;
[[nodiscard]] vec::i4 VisualOffset(const OverlayActiveSession& active) noexcept;
[[nodiscard]] float DpiScale(const OverlayActiveSession& active) noexcept;
[[nodiscard]] HWND Target(const OverlayActiveSession& active) noexcept;
[[nodiscard]] InteractionId Id(const OverlayActiveSession& active) noexcept;
[[nodiscard]] vec::i4 OriginalRawRect(const OverlayActiveSession& active) noexcept;
[[nodiscard]] const char* TypeName(SessionType type) noexcept;
void SetCursor(const OverlayActiveSession& active) noexcept;

} // namespace hw::session
