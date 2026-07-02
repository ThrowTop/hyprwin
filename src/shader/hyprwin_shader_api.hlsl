#ifndef HYPRWIN_SHADER_API_HLSL
#define HYPRWIN_SHADER_API_HLSL

#define HYPRWIN_SHADER_ABI 2

#define HYPRWIN_MAX_COLORS 16

struct HyprWinRuntime {
    float2 canvasSize;
    float2 rectCenter;

    float2 rectHalfSize;
    float gradientScale;
    uint sessionType;

    float timeSeconds;
    float deltaSeconds;
    float sessionSeconds;
};

struct HyprWinSettings {
    float2 gradientDirection;
    float borderThickness;
    float cornerRadius;

    float outerAlpha;
    float glowFalloff;
    uint colorCount;

    float4 colors[HYPRWIN_MAX_COLORS];
};

cbuffer HyprWinParams : register(b0) {
    HyprWinRuntime runtime;
    HyprWinSettings settings;
};

struct HyprWinPixelInput {
    float2 screenPosition;
};

uint hyprwin_palette_count() {
    return min(max(settings.colorCount, 1u), (uint)HYPRWIN_MAX_COLORS);
}

float4 hyprwin_palette_color(uint index) {
    uint count = hyprwin_palette_count();
    return settings.colors[min(index, count - 1u)];
}

float4 hyprwin_sample_palette(float t) {
    uint count = hyprwin_palette_count();
    float position = saturate(t) * (float)(count - 1u);
    uint left = min((uint)position, count - 1u);
    uint right = min(left + 1u, count - 1u);
    return lerp(settings.colors[left], settings.colors[right], frac(position));
}

float4 hyprwin_pixel(HyprWinPixelInput input);

float4 ps_main(float4 position : SV_Position) : SV_Target {
    HyprWinPixelInput input;
    input.screenPosition = position.xy;
    return hyprwin_pixel(input);
}

#endif
