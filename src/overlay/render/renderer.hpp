#pragma once

#include <memory>

#include <windows.h>

#include "config/settings.hpp"
#include "overlay/outline/outline_shader.hpp"
#include "overlay/cmd.hpp"
#include "overlay/render/dx_context.hpp"
#include "overlay/thumbnail/thumbnail_shader.hpp"
#include "util/geometry.hpp"

namespace hw {

enum class RenderStatus {
    Ok,
    SwapchainInvalid,
    DeviceLost,
};

inline const char* RenderStatusName(RenderStatus status) noexcept {
    switch (status) {
        case RenderStatus::Ok:
            return "Ok";
        case RenderStatus::SwapchainInvalid:
            return "SwapchainInvalid";
        case RenderStatus::DeviceLost:
            return "DeviceLost";
    }
    return "Unknown";
}

class OverlayRenderer {
  public:
    OverlayRenderer() = default;
    OverlayRenderer(const OverlayRenderer&) = delete;
    OverlayRenderer& operator=(const OverlayRenderer&) = delete;

    ~OverlayRenderer();

    [[nodiscard]] bool Init(HWND hwnd, const vec::i4& canvasBounds) noexcept;
    void Destroy() noexcept;
    bool ResetDevice() noexcept;
    void UpdateCanvas(const vec::i4& bounds) noexcept;
    void HandleDisplayChange(const vec::i4& bounds) noexcept;
    [[nodiscard]] RenderStatus ClearForShow() noexcept;
    void UseBuiltInShader(std::uint64_t generation) noexcept;
    void InstallPixelShader(std::shared_ptr<const outline::Bytecode> bytecode, std::uint64_t generation) noexcept;
    void ResetSessionAnimation() noexcept;
    [[nodiscard]] bool CaptureSnapshot(HWND target, const vec::i4& visualBounds) noexcept;
    void ClearSnapshot() noexcept;

    [[nodiscard]] RenderStatus Render(const vec::i4& visualBounds, const Settings& settings, SessionType sessionType, float dpiScale) noexcept;

  private:
    [[nodiscard]] RenderStatus EnsureReady() noexcept;
    [[nodiscard]] RenderStatus PresentClear(bool flushDwm) noexcept;
    [[nodiscard]] RenderStatus PresentDraw(bool flushDwm, bool resourcesReady) noexcept;
    void ApplySettings(const Settings& settings) noexcept;
    void UpdateGeometry() noexcept;
    void UpdateGradientDirection() noexcept;
    [[nodiscard]] outline::ShaderParams BuildShaderParams(SessionType sessionType, float dpiScale) const noexcept;

    DxContext m_dx;
    outline::Shader m_outlineShader;
    thumbnail::ThumbnailShader m_thumbnailShader;
    std::shared_ptr<const outline::Bytecode> m_customPixelShaderBytecode;
    std::uint64_t m_outlineGeneration = 0;
    bool m_pipelineDirty = true;

    HWND m_hwnd = nullptr;
    int m_canvasOriginX = 0;
    int m_canvasOriginY = 0;
    UINT m_canvasW = 0;
    UINT m_canvasH = 0;

    float m_borderX = 0.0f;
    float m_borderY = 0.0f;
    float m_borderW = 0.0f;
    float m_borderH = 0.0f;
    float m_rectCenterX = 0.0f;
    float m_rectCenterY = 0.0f;
    float m_rectHalfW = 0.0f;
    float m_rectHalfH = 0.0f;
    float m_gradientScale = 1.0f;
    bool m_settingsValid = false;
    Settings m_settings{};

    float m_gradientRuntimeAngle = 0.0f;
    float m_gradientDirX = 1.0f;
    float m_gradientDirY = 0.0f;
    float m_thumbnailCornerRadius = 0.0f;
    long long m_performanceFrequency = 0;
    long long m_animationStartTicks = 0;
    long long m_sessionStartTicks = 0;
    long long m_lastRenderTicks = 0;
    float m_shaderTimeSeconds = 0.0f;
    float m_shaderSessionSeconds = 0.0f;
    float m_shaderDeltaSeconds = 0.0f;
    SessionType m_activeSessionType = SessionType::None;
};
} // namespace hw
