#include "overlay/renderer.hpp"

#include <algorithm>
#include <cmath>

#include "log/log.hpp"
#include "perf/perf.hpp"

namespace hw {
namespace {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr bool kFlushDwmAfterCommit = true;

    const char* DxPresentStatusName(DxPresentStatus status) noexcept {
        switch (status) {
            case DxPresentStatus::Ok:
                return "Ok";
            case DxPresentStatus::SwapchainInvalid:
                return "SwapchainInvalid";
            case DxPresentStatus::DeviceLost:
                return "DeviceLost";
        }
        return "Unknown";
    }
} // namespace

OverlayRenderer::~OverlayRenderer() {
    Destroy();
}

bool OverlayRenderer::Init(HWND hwnd, const vec::i4& canvasBounds) noexcept {
    m_hwnd = hwnd;

    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency)) {
        m_performanceFrequency = frequency.QuadPart;
    }
    LARGE_INTEGER start{};
    if (QueryPerformanceCounter(&start)) {
        m_animationStartTicks = start.QuadPart;
    }

    m_canvasOriginX = canvasBounds.x;
    m_canvasOriginY = canvasBounds.y;
    m_canvasW = static_cast<UINT>(canvasBounds.Width());
    m_canvasH = static_cast<UINT>(canvasBounds.Height());

    if (!m_dx.Create(m_hwnd) || !m_shader.Create(m_dx)) {
        Destroy();
        return false;
    }

    if (!m_dx.EnsureSwapResources(m_canvasW, m_canvasH)) {
        Destroy();
        return false;
    }

    return true;
}

void OverlayRenderer::Destroy() noexcept {
    m_shader.Release();
    m_dx.ReleaseDeviceResources();

    m_borderX = 0.0f;
    m_borderY = 0.0f;
    m_borderW = 0.0f;
    m_borderH = 0.0f;
    m_rectCenterX = 0.0f;
    m_rectCenterY = 0.0f;
    m_rectHalfW = 0.0f;
    m_rectHalfH = 0.0f;
    m_gradientScale = 1.0f;
    m_settingsValid = false;
    m_lastRenderTicks = 0;
    m_sessionStartTicks = 0;
    m_gradientRuntimeAngle = 0.0f;
    m_gradientDirX = 1.0f;
    m_gradientDirY = 0.0f;
    m_shaderSessionSeconds = 0.0f;
    m_activeSessionType = SessionType::None;
    m_customPixelShaderBytecode.reset();
    m_pipelineDirty = true;
}

RenderStatus OverlayRenderer::ClearForShow() noexcept {
    return PresentClear(false);
}

bool OverlayRenderer::ResetDevice() noexcept {
    m_shader.Release();
    m_dx.ReleaseDeviceResources();

    if (!m_dx.Create(m_hwnd) || !m_shader.Create(m_dx)) {
        LOG_ERROR("renderer: ResetDevice failed during device/shader create");
        Destroy();
        return false;
    }
    m_pipelineDirty = true;

    if (m_customPixelShaderBytecode && !m_shader.InstallPixelShader(m_dx, *m_customPixelShaderBytecode)) {
        LOG_ERROR("renderer: ResetDevice failed to restore custom pixel shader; falling back to built-in");
        m_customPixelShaderBytecode.reset();
        m_shader.UseBuiltInPixelShader(m_dx);
    }

    return m_dx.EnsureSwapResources(m_canvasW, m_canvasH);
}

void OverlayRenderer::UseBuiltInShader(std::uint64_t generation) noexcept {
    if (generation < m_shaderGeneration) {
        return;
    }
    if (!m_dx.device) {
        LOG_WARN("renderer: ignored built-in pixel shader generation={} because device is unavailable", generation);
        return;
    }
    if (!m_shader.UseBuiltInPixelShader(m_dx)) {
        LOG_ERROR("renderer: failed to restore built-in pixel shader");
        return;
    }
    m_shaderGeneration = generation;
    m_customPixelShaderBytecode.reset();
    m_pipelineDirty = true;
    LOG_DEBUG("renderer: using built-in pixel shader generation={}", generation);
}

void OverlayRenderer::InstallPixelShader(std::shared_ptr<const shader::Bytecode> bytecode, std::uint64_t generation) noexcept {
    if (generation < m_shaderGeneration) {
        return;
    }
    if (!m_dx.device) {
        LOG_WARN("renderer: ignored custom pixel shader generation={} because device is unavailable", generation);
        return;
    }
    if (!bytecode || !m_shader.InstallPixelShader(m_dx, *bytecode)) {
        LOG_ERROR("renderer: failed to install custom pixel shader generation={}", generation);
        return;
    }
    m_shaderGeneration = generation;
    m_customPixelShaderBytecode = std::move(bytecode);
    m_pipelineDirty = true;
    LOG_INFO("renderer: installed custom pixel shader generation={}", generation);
}

void OverlayRenderer::ResetSessionAnimation() noexcept {
    m_activeSessionType = SessionType::None;
    m_sessionStartTicks = 0;
    m_shaderSessionSeconds = 0.0f;
}

RenderStatus OverlayRenderer::Render(const vec::i4& visualBounds, const Settings& settings, SessionType sessionType) noexcept {
    const RenderStatus ready = EnsureReady();
    if (ready != RenderStatus::Ok) {
        return ready;
    }

    const float bx = static_cast<float>(visualBounds.x - m_canvasOriginX);
    const float by = static_cast<float>(visualBounds.y - m_canvasOriginY);
    const float bw = static_cast<float>(std::max(0, visualBounds.Width()));
    const float bh = static_cast<float>(std::max(0, visualBounds.Height()));

    const bool borderChanged = (bx != m_borderX || by != m_borderY || bw != m_borderW || bh != m_borderH);
    if (borderChanged) {
        m_borderX = bx;
        m_borderY = by;
        m_borderW = bw;
        m_borderH = bh;
        UpdateGeometry();
    }

    ApplySettings(settings);

    LARGE_INTEGER now{};
    const bool haveNow = QueryPerformanceCounter(&now);
    if (haveNow && m_performanceFrequency > 0) {
        if (m_animationStartTicks == 0) {
            m_animationStartTicks = now.QuadPart;
        }

        if (m_lastRenderTicks != 0) {
            m_shaderDeltaSeconds = static_cast<float>(static_cast<double>(now.QuadPart - m_lastRenderTicks) / static_cast<double>(m_performanceFrequency));
        } else {
            m_shaderDeltaSeconds = 0.0f;
        }

        const double elapsed = static_cast<double>(now.QuadPart - m_animationStartTicks) / static_cast<double>(m_performanceFrequency);
        m_shaderTimeSeconds = static_cast<float>(std::fmod(elapsed, 3600.0));

        if (sessionType != m_activeSessionType || m_sessionStartTicks == 0) {
            m_activeSessionType = sessionType;
            m_sessionStartTicks = now.QuadPart;
        }
        if (sessionType == SessionType::None) {
            m_shaderSessionSeconds = 0.0f;
        } else {
            m_shaderSessionSeconds = static_cast<float>(static_cast<double>(now.QuadPart - m_sessionStartTicks) / static_cast<double>(m_performanceFrequency));
        }

        if (m_settings.rotating) {
            m_gradientRuntimeAngle += m_settings.rotation_speed * m_shaderDeltaSeconds;
            if (m_gradientRuntimeAngle >= 360.0f || m_gradientRuntimeAngle <= -360.0f) {
                m_gradientRuntimeAngle = std::fmod(m_gradientRuntimeAngle, 360.0f);
            }
            UpdateGradientDirection();
        }

        m_lastRenderTicks = now.QuadPart;
    }

    if (!m_shader.UpdateConstants(m_dx, BuildShaderParams(sessionType))) {
        return m_dx.ValidateDevice() == DxDeviceStatus::Ok ? RenderStatus::SwapchainInvalid : RenderStatus::DeviceLost;
    }

    return PresentDraw(kFlushDwmAfterCommit, true);
}

void OverlayRenderer::UpdateCanvas(const vec::i4& bounds) noexcept {
    const int newOriginX = bounds.x;
    const int newOriginY = bounds.y;
    const UINT newW = static_cast<UINT>(bounds.Width());
    const UINT newH = static_cast<UINT>(bounds.Height());

    if (newOriginX == m_canvasOriginX && newOriginY == m_canvasOriginY && newW == m_canvasW && newH == m_canvasH) {
        return;
    }

    m_canvasOriginX = newOriginX;
    m_canvasOriginY = newOriginY;
    m_canvasW = newW;
    m_canvasH = newH;
    m_dx.ReleaseSwapResources();
}

void OverlayRenderer::HandleDisplayChange(const vec::i4& bounds) noexcept {
    UpdateCanvas(bounds);
    m_dx.ReleaseSwapResources();
}

void OverlayRenderer::ApplySettings(const Settings& settings) noexcept {
    if (m_settingsValid && m_settings == settings) {
        return;
    }

    m_settings = settings;
    m_settingsValid = true;
    m_gradientRuntimeAngle = m_settings.gradient_angle;
    UpdateGradientDirection();
    m_lastRenderTicks = 0;
}

void OverlayRenderer::UpdateGeometry() noexcept {
    m_rectHalfW = m_borderW * 0.5f;
    m_rectHalfH = m_borderH * 0.5f;
    m_rectCenterX = m_borderX + m_rectHalfW;
    m_rectCenterY = m_borderY + m_rectHalfH;
    m_gradientScale = std::max(1.0f, std::hypot(m_rectHalfW, m_rectHalfH));
}

void OverlayRenderer::UpdateGradientDirection() noexcept {
    const float angleRad = m_gradientRuntimeAngle * (kPi / 180.0f);
    m_gradientDirX = std::cos(angleRad);
    m_gradientDirY = std::sin(angleRad);
}

RenderStatus OverlayRenderer::EnsureReady() noexcept {
    if (m_dx.ValidateDevice() != DxDeviceStatus::Ok) {
        LOG_ERROR("renderer: EnsureReady device lost swapChain={}", m_dx.swapChain ? "alive" : "null");
        return RenderStatus::DeviceLost;
    }

    if (!m_dx.EnsureSwapResources(m_canvasW, m_canvasH)) {
        m_dx.ReleaseSwapResources();
        const RenderStatus status = m_dx.ValidateDevice() == DxDeviceStatus::Ok ? RenderStatus::SwapchainInvalid : RenderStatus::DeviceLost;
        LOG_ERROR("renderer: EnsureReady EnsureSwapResources failed status={}", RenderStatusName(status));
        return status;
    }

    return RenderStatus::Ok;
}

RenderStatus OverlayRenderer::PresentClear(bool flushDwm) noexcept {
    for (int attempt = 0; attempt < 2; ++attempt) {
        const RenderStatus ready = EnsureReady();
        if (ready != RenderStatus::Ok) {
            LOG_ERROR("renderer: PresentClear EnsureReady failed status={}", RenderStatusName(ready));
            return ready;
        }

        m_dx.ClearRenderTarget();
        const DxPresentStatus present = m_dx.PresentAndCommit(flushDwm);
        if (present == DxPresentStatus::Ok) {
            return RenderStatus::Ok;
        }
        LOG_ERROR("renderer: PresentClear failed status={} attempt={}", DxPresentStatusName(present), attempt);
        if (present == DxPresentStatus::DeviceLost) {
            return RenderStatus::DeviceLost;
        }

        m_dx.ReleaseSwapResources();
    }

    return RenderStatus::SwapchainInvalid;
}

RenderStatus OverlayRenderer::PresentDraw(bool flushDwm, bool resourcesReady) noexcept {
    HW_PERF_SCOPE(::perf::CounterId::OverlayFrame);

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!resourcesReady || attempt > 0) {
            const RenderStatus ready = EnsureReady();
            if (ready != RenderStatus::Ok) {
                LOG_ERROR("renderer: PresentDraw EnsureReady status={}", RenderStatusName(ready));
                return ready;
            }
        }

        m_dx.ClearRenderTarget();
        m_dx.SetViewport(m_canvasW, m_canvasH);
        if (m_pipelineDirty) {
            m_shader.Bind(m_dx);
            m_pipelineDirty = false;
        }
        m_dx.context->Draw(3, 0);

        const DxPresentStatus present = m_dx.PresentAndCommit(flushDwm);
        if (present == DxPresentStatus::Ok) {
            return RenderStatus::Ok;
        }
        LOG_ERROR("renderer: PresentDraw present status={} attempt={}", DxPresentStatusName(present), attempt);
        if (present == DxPresentStatus::DeviceLost) {
            return RenderStatus::DeviceLost;
        }

        m_dx.ReleaseSwapResources();
    }

    return RenderStatus::SwapchainInvalid;
}

ShaderParams OverlayRenderer::BuildShaderParams(SessionType sessionType) const noexcept {
    ShaderParams params{};
    params.runtime.canvasSize[0] = static_cast<float>(m_canvasW);
    params.runtime.canvasSize[1] = static_cast<float>(m_canvasH);
    params.runtime.rectCenter[0] = m_rectCenterX;
    params.runtime.rectCenter[1] = m_rectCenterY;
    params.runtime.rectHalfSize[0] = m_rectHalfW;
    params.runtime.rectHalfSize[1] = m_rectHalfH;
    params.runtime.gradientScale = m_gradientScale;
    params.runtime.sessionType = static_cast<std::uint32_t>(sessionType == SessionType::Drag     ? ShaderSession::Drag
                                                            : sessionType == SessionType::Resize ? ShaderSession::Resize
                                                                                                 : ShaderSession::None);
    params.runtime.timeSeconds = m_shaderTimeSeconds;
    params.runtime.deltaSeconds = m_shaderDeltaSeconds;
    params.runtime.sessionSeconds = m_shaderSessionSeconds;

    params.settings.gradientDirection[0] = m_gradientDirX;
    params.settings.gradientDirection[1] = m_gradientDirY;
    params.settings.borderThickness = m_settings.border;
    params.settings.cornerRadius = m_settings.corner_radius;
    params.settings.outerAlpha = m_settings.outer_alpha;
    params.settings.glowFalloff = m_settings.glow_falloff;
    params.settings.colorCount = m_settings.shader_palette.count;

    const std::uint32_t colorCount = std::min<std::uint32_t>(m_settings.shader_palette.count, kMaxShaderColors);
    for (std::uint32_t i = 0; i < colorCount; ++i) {
        const Color color = m_settings.shader_palette.colors[i];
        params.settings.colors[i][0] = hw::Color::ToFloat(color.r);
        params.settings.colors[i][1] = hw::Color::ToFloat(color.g);
        params.settings.colors[i][2] = hw::Color::ToFloat(color.b);
        params.settings.colors[i][3] = hw::Color::ToFloat(color.a);
    }
    return params;
}
} // namespace hw
