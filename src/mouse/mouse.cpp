#include "mouse/mouse.hpp"

#include "log/log.hpp"
#include "mouse/selection.hpp"
#include "overlay/cmd.hpp"
#include "perf/perf.hpp"
#include "util/strings.hpp"
#include "util/thread_priority.hpp"
#include "win/native.hpp"

#include <dwmapi.h>

namespace hw {
namespace {
    const char* FilterReasonName(win::WindowFilterReason reason) noexcept {
        switch (reason) {
            case win::WindowFilterReason::None:
                return "none";
            case win::WindowFilterReason::Invalid:
                return "invalid";
            case win::WindowFilterReason::NoRoot:
                return "no-root";
            case win::WindowFilterReason::Minimized:
                return "minimized";
            case win::WindowFilterReason::Invisible:
                return "invisible";
            case win::WindowFilterReason::Cloaked:
                return "cloaked";
            case win::WindowFilterReason::ShellProtected:
                return "shell-protected";
            case win::WindowFilterReason::Child:
                return "child";
            case win::WindowFilterReason::ToolWindow:
                return "tool-window";
            case win::WindowFilterReason::NoActivate:
                return "no-activate";
            case win::WindowFilterReason::NoProcess:
                return "no-process";
            case win::WindowFilterReason::RuleExcluded:
                return "rule-excluded";
            case win::WindowFilterReason::OutsideVisualBounds:
                return "outside-visual-bounds";
        }
        return "unknown";
    }

    void TraceGrabAttempt(bool enabled, bool isLeft, POINT pt, const win::WindowAtPointResult& selection, std::string_view outcome) {
        if (!enabled) {
            return;
        }

        const HWND inspected = selection.candidate ? selection.candidate : (selection.top ? selection.top : selection.hit);
        const DWORD pid = win::GetProcessId(inspected);
        const std::string process = ::util::WideToUtf8(win::GetProcessName(inspected));
        const std::string title = ::util::WideToUtf8(win::GetWindowTitle(inspected));
        const std::string windowClass = ::util::WideToUtf8(win::GetWindowClass(inspected));
        const LONG_PTR style = inspected ? GetWindowLongPtrW(inspected, GWL_STYLE) : 0;
        const LONG_PTR exStyle = inspected ? GetWindowLongPtrW(inspected, GWL_EXSTYLE) : 0;
        const bool visible = inspected && IsWindowVisible(inspected);
        BOOL cloakedValue = FALSE;
        const bool cloaked = inspected && SUCCEEDED(DwmGetWindowAttribute(inspected, DWMWA_CLOAKED, &cloakedValue, sizeof(cloakedValue))) && cloakedValue;
        const bool responsive = inspected && win::IsWindowResponsive(inspected);
        const bool resizable = inspected && win::GetResizable(inspected);
        const bool maximized = inspected && win::GetMaximized(inspected);
        const bool minimized = inspected && win::GetMinimized(inspected);

        RECT rawRect{};
        RECT visualRect{};
        const bool hasRawRect = inspected && win::GetRawWindowRect(inspected, rawRect);
        const bool hasVisualRect = inspected && win::GetVisualWindowRect(inspected, visualRect);
        const bool fullscreen = hasRawRect && win::GetBorderlessFullscreen(inspected, rawRect);
        std::string filter = FilterReasonName(selection.rejection);
        if (!selection.matched_rule.empty()) {
            filter = std::format("{} ({})", filter, selection.matched_rule);
        } else if (selection.matched_rule_index != static_cast<std::size_t>(-1)) {
            filter = std::format("{} (custom[{}])", filter, selection.matched_rule_index + 1);
        }

        LOG_INFO("mouse: grab attempt: {}\n"
                 "  result:    {}\n"
                 "  operation: {}\n"
                 "  window:    \"{}\"\n"
                 "  identity:  pid={} class=\"{}\"\n"
                 "  handles:   hit={:p} top={:p} candidate={:p} inspected={:p}\n"
                 "  filter:    {}\n"
                 "  state:     visible={} cloaked={} responsive={} resizable={}\n"
                 "             maximized={} minimized={} fullscreen={}\n"
                 "  geometry:  cursor=({}, {})\n"
                 "             raw={} visual={}\n"
                 "  styles:    normal=0x{:X} extended=0x{:X}",
          process.empty() ? "<unknown>" : process,
          outcome,
          isLeft ? "drag" : "resize",
          title,
          pid,
          windowClass,
          reinterpret_cast<void*>(selection.hit),
          reinterpret_cast<void*>(selection.top),
          reinterpret_cast<void*>(selection.candidate),
          reinterpret_cast<void*>(inspected),
          filter,
          visible,
          cloaked,
          responsive,
          resizable,
          maximized,
          minimized,
          fullscreen,
          pt.x,
          pt.y,
          hasRawRect ? std::format("[{}, {}, {}, {}]", rawRect.left, rawRect.top, rawRect.right, rawRect.bottom) : "unavailable",
          hasVisualRect ? std::format("[{}, {}, {}, {}]", visualRect.left, visualRect.top, visualRect.right, visualRect.bottom) : "unavailable",
          static_cast<std::uintptr_t>(style),
          static_cast<std::uintptr_t>(exStyle));
    }

    bool IsLeftEvent(WPARAM event) noexcept {
        return event == WM_LBUTTONDOWN || event == WM_LBUTTONUP;
    }

    bool IsDownEvent(WPARAM event) noexcept {
        return event == WM_LBUTTONDOWN || event == WM_RBUTTONDOWN;
    }

    bool RestoreMaximizedForOperation(HWND hwnd, vec::i2 cursor, vec::i4& rawRect) noexcept {
        if (!win::GetMaximized(hwnd))
            return true;

        const vec::i2 maximizedSize = rawRect.Size();
        if (maximizedSize.x <= 0 || maximizedSize.y <= 0)
            return true;

        const vec::i2 cursorInWindow = cursor - rawRect.Pos();

        // blocks: ShowWindow sends WM_SIZE/WM_SHOWWINDOW to target message pump
        if (!win::IsWindowResponsive(hwnd)) {
            LOG_WARN("mouse: RestoreMaximized: {:p} not responding", reinterpret_cast<void*>(hwnd));
            return false;
        }
        ShowWindow(hwnd, SW_RESTORE);

        RECT restoredWin{};
        if (!win::GetRawWindowRect(hwnd, restoredWin)) {
            LOG_WARN("mouse: GetRawWindowRect failed after SW_RESTORE on {:p}", reinterpret_cast<void*>(hwnd));
            return false;
        }
        rawRect = vec::i4::FromWin32(restoredWin);

        const vec::i2 restoredSize = rawRect.Size();
        const vec::i2 anchor{
          cursorInWindow.x * restoredSize.x / maximizedSize.x,
          cursorInWindow.y * restoredSize.y / maximizedSize.y,
        };
        const vec::i2 newPos = cursor - anchor;
        const vec::i4 reanchoredRawRect = rawRect.WithPos(newPos.x, newPos.y);
        if (!win::MoveWindowToRawRect(hwnd, reanchoredRawRect.ToWin32())) {
            LOG_WARN("mouse: failed to re-anchor restored target {:p} raw_rect={}", reinterpret_cast<void*>(hwnd), reanchoredRawRect);
            return false;
        }

        RECT finalWin{};
        const bool ok = win::GetRawWindowRect(hwnd, finalWin);
        rawRect = vec::i4::FromWin32(finalWin);
        return ok;
    }
} // namespace

Mouse::Mouse(std::atomic<POINT>* latestMousePos, AtomicSettingsPtr* settings, OverlayService* overlay) noexcept
    : m_latestMousePos(latestMousePos)
    , m_settings(settings)
    , m_overlay(overlay) {
    m_instance.store(this, std::memory_order_release);

    m_dispatchThread = std::jthread([this](std::stop_token token) {
        const ::util::ScopedThreadPriorityBoost priorityBoost;
        SET_THREAD_NAME("M-D");
        DispatchThreadMain(token);
    });
    m_hookThread = std::jthread([this](std::stop_token token) {
        const ::util::ScopedThreadPriorityBoost priorityBoost;
        SET_THREAD_NAME("M-H");
        HookThreadMain(token);
    });
}

Mouse::~Mouse() {
    Mouse* expected = this;
    m_instance.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);

    m_uninstallRequested.store(true, std::memory_order_release);
    m_installRequested.store(false, std::memory_order_release);

    m_hookThread.request_stop();
    m_dispatchThread.request_stop();

    const DWORD dtid = m_hookThreadId.load(std::memory_order_acquire);
    if (dtid != 0) {
        PostThreadMessageW(dtid, WM_NULL, 0, 0);
    }
    m_hookCv.notify_all();
    m_dispatchCv.notify_all();

    if (m_hookThread.joinable()) {
        m_hookThread.join();
    }
    if (m_dispatchThread.joinable()) {
        m_dispatchThread.join();
    }
}

void Mouse::InstallHook() noexcept {
    m_installRequested.store(true, std::memory_order_release);
    m_uninstallRequested.store(false, std::memory_order_release);
    m_hookCv.notify_one();
}

void Mouse::UninstallHook() noexcept {
    m_uninstallRequested.store(true, std::memory_order_release);
    m_installRequested.store(false, std::memory_order_release);

    m_cancelRequested.store(true, std::memory_order_release);
    m_dispatchCv.notify_one();

    const DWORD utid = m_hookThreadId.load(std::memory_order_acquire);
    if (utid != 0) {
        PostThreadMessageW(utid, WM_NULL, 0, 0);
    }
    m_hookCv.notify_one();
}

LRESULT CALLBACK Mouse::HookProc(int code, WPARAM wparam, LPARAM lparam) noexcept {
    HW_PERF_SCOPE(::perf::CounterId::MouseHook);

    Mouse* self = m_instance.load(std::memory_order_acquire);
    if (code != HC_ACTION || self == nullptr || lparam == 0) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    const auto* event = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);

    if (event->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED)) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    switch (wparam) {
        case WM_MOUSEMOVE:
            if (self->m_latestMousePos) {
                self->m_latestMousePos->store(event->pt, std::memory_order_relaxed);
            }
            return CallNextHookEx(nullptr, code, wparam, lparam);

        case WM_LBUTTONDOWN:
            self->m_lastDownPt.store(event->pt, std::memory_order_relaxed);
            self->PushButtonEvent(wparam);
            return 1;

        case WM_RBUTTONDOWN:
            self->m_lastDownPt.store(event->pt, std::memory_order_relaxed);
            self->PushButtonEvent(wparam);
            return 1;

        case WM_LBUTTONUP:
            self->PushButtonEvent(wparam);
            if (self->m_allowLeftUpPassthrough.exchange(false, std::memory_order_acq_rel)) {
                return CallNextHookEx(nullptr, code, wparam, lparam);
            }
            return 1;

        case WM_RBUTTONUP:
            self->PushButtonEvent(wparam);
            if (self->m_allowRightUpPassthrough.exchange(false, std::memory_order_acq_rel)) {
                return CallNextHookEx(nullptr, code, wparam, lparam);
            }
            return 1;

        case WM_MBUTTONDOWN:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            return 1;

        default:
            return CallNextHookEx(nullptr, code, wparam, lparam);
    }
}

void Mouse::HookThreadMain(std::stop_token token) noexcept {
    m_hookThreadId.store(GetCurrentThreadId(), std::memory_order_release);

    while (!token.stop_requested()) {
        std::unique_lock lock(m_hookMutex);
        m_hookCv.wait(lock, [&] { return token.stop_requested() || m_installRequested.load(std::memory_order_acquire); });
        lock.unlock();

        if (token.stop_requested()) {
            break;
        }
        m_installRequested.store(false, std::memory_order_release);
        if (m_uninstallRequested.load(std::memory_order_acquire)) {
            m_uninstallRequested.store(false, std::memory_order_release);
            continue;
        }

        m_allowLeftUpPassthrough.store((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, std::memory_order_release);
        m_allowRightUpPassthrough.store((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0, std::memory_order_release);

        m_hook = SetWindowsHookExW(WH_MOUSE_LL, HookProc, nullptr, 0);
        if (!m_hook) {
            LOG_ERROR("failed to install WH_MOUSE_LL hook: {}", GetLastError());
            continue;
        }

        MSG message{};
        while (!token.stop_requested()) {
            if (m_uninstallRequested.load(std::memory_order_acquire)) {
                break;
            }

            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result <= 0) {
                break;
            }
        }

        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
        m_uninstallRequested.store(false, std::memory_order_release);
        m_allowLeftUpPassthrough.store(false, std::memory_order_release);
        m_allowRightUpPassthrough.store(false, std::memory_order_release);
    }

    m_hookThreadId.store(0, std::memory_order_release);
}

void Mouse::DispatchThreadMain(std::stop_token token) noexcept {
    while (!token.stop_requested()) {
        std::unique_lock lock(m_dispatchMutex);
        m_dispatchCv.wait(lock, [&] {
            return token.stop_requested() || !m_buttonQueue.empty() || m_queueOverflowed.load(std::memory_order_acquire) || m_cancelRequested.load(std::memory_order_acquire);
        });
        lock.unlock();

        if (m_cancelRequested.exchange(false, std::memory_order_acq_rel)) {
            CancelOperation();
        }

        if (m_queueOverflowed.exchange(false, std::memory_order_acq_rel)) {
            LOG_ERROR("mouse button queue overflowed, cancelling active operation (HOW DID U DO IT)");
            CancelOperation();
        }

        WPARAM event{};
        while (m_buttonQueue.pop(event)) {
            ProcessButtonEvent(event);
        }
    }
}

bool Mouse::PushButtonEvent(WPARAM event) noexcept {
    if (m_buttonQueue.push(event)) {
        m_dispatchCv.notify_one();
        return true;
    }

    m_queueOverflowed.store(true, std::memory_order_release);
    m_dispatchCv.notify_one();
    return false;
}

void Mouse::ProcessButtonEvent(WPARAM event) noexcept {
    if (IsDownEvent(event)) {
        BeginOperation(event);
    } else if (m_target) {
        const SessionType expected = IsLeftEvent(event) ? SessionType::Drag : SessionType::Resize;
        if (m_sessionType == expected) {
            FinishOperation();
        }
    }
}

void Mouse::BeginOperation(WPARAM event) noexcept {
    if (m_target || !m_overlay) {
        return;
    }

    const bool isLeft = IsLeftEvent(event);

    const POINT ptWin = m_lastDownPt.load(std::memory_order_relaxed);
    if (m_latestMousePos) {
        m_latestMousePos->store(ptWin, std::memory_order_relaxed);
    }

    const SettingsPtr settings = LoadSettingsSnapshot(m_settings, DefaultSettings());
    const bool traceGrabs = settings && settings->debug.trace_grabs;
    const win::WindowAtPointResult selection = mouse::SelectTarget(ptWin, *settings);
    HWND candidate = selection.candidate;
    if (!candidate) {
        TraceGrabAttempt(traceGrabs, isLeft, ptWin, selection, "rejected:no-candidate");
        return;
    }

    if (!win::IsWindowResponsive(candidate)) {
        TraceGrabAttempt(traceGrabs, isLeft, ptWin, selection, "rejected:unresponsive");
        const std::string pname = ::util::WideToUtf8(win::GetProcessName(candidate));
        LOG_WARN("mouse: target {} ({}) is not responding, aborting operation", reinterpret_cast<void*>(candidate), pname);
        return;
    }

    RECT rawRectWin{};
    if (!win::GetRawWindowRect(candidate, rawRectWin)) {
        TraceGrabAttempt(traceGrabs, isLeft, ptWin, selection, "rejected:no-raw-rect");
        return;
    }
    if (win::GetBorderlessFullscreen(candidate, rawRectWin)) {
        TraceGrabAttempt(traceGrabs, isLeft, ptWin, selection, "rejected:fullscreen");
        return;
    }

    if (!settings) {
        TraceGrabAttempt(traceGrabs, isLeft, ptWin, selection, "rejected:no-settings");
        return;
    }

    if (!isLeft && !win::GetResizable(candidate)) {
        TraceGrabAttempt(traceGrabs, isLeft, ptWin, selection, "rejected:non-resizable");
        return;
    }

    win::FocusWindow(candidate);
    m_target = candidate;
    m_sessionType = isLeft ? SessionType::Drag : SessionType::Resize;

    const vec::i2 pt = vec::i2::FromWin32(ptWin);
    vec::i4 rawRect = vec::i4::FromWin32(rawRectWin);

    if (!RestoreMaximizedForOperation(candidate, pt, rawRect)) {
        TraceGrabAttempt(traceGrabs, isLeft, ptWin, selection, "failed:restore-maximized");
        const std::string pname = ::util::WideToUtf8(win::GetProcessName(candidate));
        LOG_WARN("mouse: hw::RestoreMaximizedForOperation failed for {} ({})", reinterpret_cast<void*>(candidate), pname);
        m_target = nullptr;
        m_sessionType = SessionType::None;
        return;
    }

    RECT visualOffsetWin{};
    win::GetDwmVisualOffsets(candidate, visualOffsetWin);
    const vec::i4 visualOffset = vec::i4::FromWin32(visualOffsetWin);
    const UINT windowDpi = GetDpiForWindow(candidate);
    const float dpiScale = windowDpi != 0 ? static_cast<float>(windowDpi) / 96.0f : 1.0f;

    if (isLeft) {
        const vec::i2 anchor{pt.x - rawRect.x, pt.y - rawRect.y};
        DragSession session{
          .anchor = anchor,
          .windowSize = rawRect.Size(),
          .visualOffset = visualOffset,
          .dpiScale = dpiScale,
        };
        if (!m_overlay->Send(BeginDrag{.session = session, .initialBounds = rawRect})) {
            TraceGrabAttempt(traceGrabs, isLeft, ptWin, selection, "failed:overlay-queue");
            const std::string pname = ::util::WideToUtf8(win::GetProcessName(candidate));
            LOG_WARN("mouse: failed to start drag for {} ({}) raw_rect={}", reinterpret_cast<void*>(candidate), pname, rawRect);
            m_target = nullptr;
            m_sessionType = SessionType::None;
            return;
        }
        TraceGrabAttempt(traceGrabs, isLeft, ptWin, selection, "accepted:drag");
        return;
    }

    SIZE minSizeWin{};
    SIZE maxSizeWin{};
    win::GetMinMaxInfo(candidate, minSizeWin, maxSizeWin);
    const ResizeCorner corner = ResolveResizeCorner(*settings, pt, rawRect);

    ResizeSession session{
      .startCursor = pt,
      .startRect = rawRect,
      .corner = corner,
      .minSize = vec::i2{static_cast<int>(minSizeWin.cx), static_cast<int>(minSizeWin.cy)},
      .maxSize = vec::i2{static_cast<int>(maxSizeWin.cx), static_cast<int>(maxSizeWin.cy)},
      .visualOffset = visualOffset,
      .dpiScale = dpiScale,
    };
    if (!m_overlay->Send(BeginResize{.session = session})) {
        TraceGrabAttempt(traceGrabs, isLeft, ptWin, selection, "failed:overlay-queue");
        const std::string pname = ::util::WideToUtf8(win::GetProcessName(candidate));
        LOG_WARN("mouse: failed to start resize for {} ({}) raw_rect={}", reinterpret_cast<void*>(candidate), pname, rawRect);
        m_target = nullptr;
        m_sessionType = SessionType::None;
        return;
    }
    TraceGrabAttempt(traceGrabs, isLeft, ptWin, selection, "accepted:resize");
}

void Mouse::FinishOperation() noexcept {
    if (!m_target || !m_overlay) {
        m_target = nullptr;
        m_sessionType = SessionType::None;
        return;
    }

    const vec::i4 bounds = m_overlay->GetLatestBounds();
    m_overlay->Send(Hide{});

    if (bounds.Width() > 0 && bounds.Height() > 0) {
        if (win::GetMaximized(m_target)) {
            ShowWindow(m_target, SW_RESTORE);
        }
        if (!win::PostMoveWindowToRawRect(m_target, bounds.ToWin32())) {
            const std::string pname = ::util::WideToUtf8(win::GetProcessName(m_target));
            LOG_WARN(
              "mouse: failed to commit {} for {} ({}) raw_rect={}", m_sessionType == SessionType::Drag ? "drag" : "resize", reinterpret_cast<void*>(m_target), pname, bounds);
        }
    }

    m_target = nullptr;
    m_sessionType = SessionType::None;
}

void Mouse::CancelOperation() noexcept {
    if (m_overlay) {
        m_overlay->Send(Hide{});
    }
    m_target = nullptr;
    m_sessionType = SessionType::None;
}

ResizeCorner Mouse::ResolveResizeCorner(const Settings& settings, vec::i2 pt, const vec::i4& rawRect) const noexcept {
    if (settings.resize_corner != ResizeCorner::Closest) {
        return settings.resize_corner;
    }

    const int width = rawRect.Width();
    const int height = rawRect.Height();
    if (width <= 0 || height <= 0) {
        return ResizeCorner::BottomRight;
    }

    const bool left = (pt.x - rawRect.x) < width / 2;
    const bool top = (pt.y - rawRect.y) < height / 2;
    if (left) {
        return top ? ResizeCorner::TopLeft : ResizeCorner::BottomLeft;
    }

    return top ? ResizeCorner::TopRight : ResizeCorner::BottomRight;
}
} // namespace hw
