#include "overlay/service.hpp"

#include "log/log.hpp"
#include "overlay/bounds.hpp"
#include "overlay/overlay_window.hpp"
#include "overlay/renderer.hpp"
#include "util/thread_priority.hpp"

namespace hw {
namespace {
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

        return vec::i4{};
    }

    vec::i4 VisualOffset(const OverlayActiveSession& active) noexcept {
        if (const auto* drag = std::get_if<DragSession>(&active)) {
            return drag->visualOffset;
        }

        if (const auto* resize = std::get_if<ResizeSession>(&active)) {
            return resize->visualOffset;
        }

        return vec::i4{};
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

    void SetOverlayCursor(const OverlayActiveSession& active) noexcept {
        static HCURSOR move = LoadCursorW(nullptr, IDC_SIZEALL);
        static HCURSOR resizeNwse = LoadCursorW(nullptr, IDC_SIZENWSE);
        static HCURSOR resizeNesw = LoadCursorW(nullptr, IDC_SIZENESW);
        static HCURSOR arrow = LoadCursorW(nullptr, IDC_ARROW);

        if (std::holds_alternative<DragSession>(active)) {
            SetCursor(move);
            return;
        }

        if (const auto* resize = std::get_if<ResizeSession>(&active)) {
            switch (resize->corner) {
                case ResizeCorner::TopLeft:
                case ResizeCorner::BottomRight:
                    SetCursor(resizeNwse);
                    return;
                case ResizeCorner::TopRight:
                case ResizeCorner::BottomLeft:
                    SetCursor(resizeNesw);
                    return;
                case ResizeCorner::Closest:
                    break;
            }
        }

        SetCursor(arrow);
    }

    bool RecoverRenderer(OverlayRenderer& renderer, RenderStatus status) noexcept {
        switch (status) {
            case RenderStatus::Ok:
                return true;
            case RenderStatus::SwapchainInvalid:
            case RenderStatus::DeviceLost:
                if (renderer.ResetDevice()) {
                    LOG_DEBUG("overlay_service: renderer recovery succeeded status={}", RenderStatusName(status));
                    return true;
                }
                LOG_ERROR("overlay_service: renderer recovery failed status={}", RenderStatusName(status));
                return false;
        }

        return false;
    }

    void HandleDisplayChange(OverlayWindow& window, OverlayRenderer& renderer) noexcept {
        window.ForceVirtualDesktopReapply();
        renderer.HandleDisplayChange(window.Bounds());
    }
} // namespace

OverlayService::OverlayService(HINSTANCE instance, std::atomic<POINT>* latestMousePos, AtomicSettingsPtr* settings)
    : m_instance(instance)
    , m_latestMousePos(latestMousePos)
    , m_settings(settings)
    , m_shaderManager([this](OverlayCmd cmd) { PublishShaderCommand(std::move(cmd)); }) {}

OverlayService::~OverlayService() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_cv.notify_one();
    }
}

void OverlayService::Start() {
    std::lock_guard lock(m_mutex);
    if (m_started) {
        return;
    }

    m_started = true;
    m_thread = std::jthread([this](std::stop_token token) {
        const ::util::ScopedThreadPriorityBoost priorityBoost;
        SET_THREAD_NAME("OVR");
        OverlayLoop(token);
    });
}

bool OverlayService::Send(const OverlayCmd& cmd) noexcept {
    if (!m_commands.push(cmd)) {
        LOG_WARN("Overlay command queue full; dropping command");
        return false;
    }

    m_cv.notify_one();
    return true;
}

void OverlayService::MarkSettingsDirty() noexcept {
    m_settingsDirty.store(true, std::memory_order_release);
    m_cv.notify_one();
}

void OverlayService::PublishShaderCommand(OverlayCmd cmd) noexcept {
    {
        std::lock_guard lock(m_mutex);
        m_pendingShaderCommand = std::move(cmd);
    }
    m_cv.notify_one();
}

vec::i4 OverlayService::GetLatestBounds() const noexcept {
    return m_latestBounds.Load();
}

void OverlayService::OverlayLoop(std::stop_token token) noexcept {
    OverlayWindow window;
    OverlayRenderer renderer;

    if (!window.Init(m_instance)) {
        LOG_CRITICAL("Failed to initialize overlay window");
        return;
    }

    if (!renderer.Init(window.hwnd(), window.Bounds())) {
        LOG_CRITICAL("Failed to initialize overlay renderer");
        return;
    }

    SettingsPtr settingsSnapshot = LoadSettingsSnapshot(m_settings, DefaultSettings());
    m_shaderManager.ApplySettings(*settingsSnapshot);
    if (!RecoverRenderer(renderer, renderer.Render(window.Bounds(), *settingsSnapshot, SessionType::None, 1.0f))) {
        LOG_CRITICAL("Failed to prime overlay renderer");
        return;
    }

    OverlayActiveSession active;
    bool shutdown = false;
    bool resetDevice = false;

    while (!token.stop_requested() && !shutdown) {
        if (!IsActive(active)) {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&] { return token.stop_requested() || !m_commands.empty() || m_pendingShaderCommand.has_value() || m_settingsDirty.load(std::memory_order_relaxed); });
            lock.unlock();

            if (token.stop_requested()) {
                break;
            }

            window.PumpMessages();
            if (window.ConsumeDisplayChanged()) {
                HandleDisplayChange(window, renderer);
            }

            DrainCommands(renderer, active, settingsSnapshot, shutdown, resetDevice);
            if (resetDevice) {
                if (!renderer.ResetDevice()) {
                    active = std::monostate{};
                }
                resetDevice = false;
            }

            if (!IsActive(active)) {
                continue;
            }

            window.UpdateVirtualDesktop();
            renderer.UpdateCanvas(window.Bounds());
            RenderStatus clearStatus = renderer.ClearForShow();
            if (clearStatus != RenderStatus::Ok) {
                LOG_ERROR("overlay_service: ClearForShow failed status={}", RenderStatusName(clearStatus));
                if (!RecoverRenderer(renderer, clearStatus)) {
                    LOG_ERROR("overlay_service: ClearForShow recovery failed status={}", RenderStatusName(clearStatus));
                    active = std::monostate{};
                    continue;
                }

                clearStatus = renderer.ClearForShow();
                if (clearStatus != RenderStatus::Ok) {
                    LOG_ERROR("overlay_service: ClearForShow failed after recovery status={}", RenderStatusName(clearStatus));
                    active = std::monostate{};
                    continue;
                }
            }
            window.Show();
        }

        window.PumpMessages();
        if (window.ConsumeDisplayChanged()) {
            HandleDisplayChange(window, renderer);
        }
        DrainCommands(renderer, active, settingsSnapshot, shutdown, resetDevice);
        if (resetDevice) {
            if (!renderer.ResetDevice()) {
                active = std::monostate{};
            } else {
                window.ForceVirtualDesktopReapply();
            }
            resetDevice = false;
        }

        if (shutdown || token.stop_requested()) {
            break;
        }

        if (!IsActive(active)) {
            window.Hide();
            continue;
        }

        const POINT cursor = m_latestMousePos ? m_latestMousePos->load(std::memory_order_relaxed) : POINT{};
        const vec::i4 logicalBounds = ComputeBounds(active, cursor);
        m_latestBounds.Store(logicalBounds);

        const vec::i4 visualBounds = ApplyVisualOffset(logicalBounds, VisualOffset(active));

        const SessionType sessionType = GetSessionType(active);
        const RenderStatus renderStatus = renderer.Render(visualBounds, *settingsSnapshot, sessionType, DpiScale(active));
        if (renderStatus != RenderStatus::Ok) {
            LOG_ERROR("overlay_service: Render failed status={}", RenderStatusName(renderStatus));
            if (!RecoverRenderer(renderer, renderStatus)) {
                active = std::monostate{};
            }
        }
    }

    window.Hide();
    renderer.Destroy();
    window.Destroy();
}

void OverlayService::DrainCommands(OverlayRenderer& renderer, OverlayActiveSession& active, SettingsPtr& settingsSnapshot, bool& shutdown, bool& resetDevice) noexcept {
    if (m_settingsDirty.exchange(false, std::memory_order_acq_rel)) {
        settingsSnapshot = LoadSettingsSnapshot(m_settings, settingsSnapshot);
        m_shaderManager.ApplySettings(*settingsSnapshot);
    }

    std::optional<OverlayCmd> shaderCommand;
    {
        std::lock_guard lock(m_mutex);
        shaderCommand = std::move(m_pendingShaderCommand);
        m_pendingShaderCommand.reset();
    }
    if (shaderCommand) {
        ApplyCommand(renderer, *shaderCommand, active, settingsSnapshot, shutdown, resetDevice);
        if (shutdown) {
            return;
        }
    }

    OverlayCmd cmd;
    while (m_commands.pop(cmd)) {
        ApplyCommand(renderer, cmd, active, settingsSnapshot, shutdown, resetDevice);
        if (shutdown) {
            return;
        }
    }
}

void OverlayService::ApplyCommand(
  OverlayRenderer& renderer, const OverlayCmd& cmd, OverlayActiveSession& active, SettingsPtr& settingsSnapshot, bool& shutdown, bool& resetDevice) noexcept {
    std::visit(
      [&](const auto& value) noexcept {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, Hide>) {
              active = std::monostate{};
              SetOverlayCursor(active);
          } else if constexpr (std::is_same_v<T, ResetDevice>) {
              resetDevice = true;
          } else if constexpr (std::is_same_v<T, Shutdown>) {
              active = std::monostate{};
              SetOverlayCursor(active);
              shutdown = true;
          } else if constexpr (std::is_same_v<T, UseBuiltInShader>) {
              renderer.UseBuiltInShader(value.generation);
          } else if constexpr (std::is_same_v<T, InstallPixelShader>) {
              renderer.InstallPixelShader(value.bytecode, value.generation);
          } else if constexpr (std::is_same_v<T, BeginDrag>) {
              settingsSnapshot = LoadSettingsSnapshot(m_settings, settingsSnapshot);
              renderer.ResetSessionAnimation();
              active = value.session;
              m_latestBounds.Store(InitialBounds(value.session, value));
              SetOverlayCursor(active);
          } else if constexpr (std::is_same_v<T, BeginResize>) {
              settingsSnapshot = LoadSettingsSnapshot(m_settings, settingsSnapshot);
              renderer.ResetSessionAnimation();
              active = value.session;
              m_latestBounds.Store(value.session.startRect);
              SetOverlayCursor(active);
          }
      },
      cmd);
}

} // namespace hw
