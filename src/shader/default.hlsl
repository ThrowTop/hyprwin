#include "hyprwin_shader_api.hlsl"

struct VSOut {
    float4 pos : SV_Position;
};

VSOut vs_main(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float rounded_box_distance(float2 p, float2 halfSize, float radius) {
    float2 q = abs(p) - halfSize + radius;
    return length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - radius;
}

float4 hyprwin_pixel(HyprWinPixelInput input) {
    float2 p = input.screenPosition - runtime.rectCenter;
    float d = rounded_box_distance(p, runtime.rectHalfSize, settings.cornerRadius);

    float outerThickness = floor(max(settings.borderThickness, 1.0f) * 0.5f);
    float innerThickness = max(settings.borderThickness, 1.0f) - outerThickness;

    float t = saturate(dot(p, settings.gradientDirection) / (runtime.gradientScale * 2.0f) + 0.5f);
    float4 innerColor = hyprwin_sample_palette(t);
    float4 outerColor = float4(innerColor.rgb, innerColor.a * settings.outerAlpha);

    float outerMask = smoothstep(1.0f, -1.0f, d) *
                      smoothstep(-outerThickness - 1.0f, -outerThickness + 1.0f, d);

    float innerMask = smoothstep(-outerThickness + 1.0f, -outerThickness - 1.0f, d) *
                      smoothstep(-outerThickness - innerThickness - 1.0f,
                                 -outerThickness - innerThickness + 1.0f, d);

    float glowIntensity = exp(-max(d, 0.0f) * settings.glowFalloff) * smoothstep(-1.0f, 1.0f, d);

    float finalAlpha = saturate(
        outerColor.a * (outerMask + glowIntensity) +
        innerColor.a * innerMask
    );

    return float4(innerColor.rgb * finalAlpha, finalAlpha);
}
