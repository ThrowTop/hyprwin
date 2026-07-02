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

    float t = saturate(dot(p, settings.gradientDirection) / (runtime.gradientScale * 2.0f) + 0.5f);
    float4 color = hyprwin_sample_palette(t);

    float borderDistance = d + 1.0f;
    float thickness = max(settings.borderThickness, 1.0f);
    float aa = max(fwidth(borderDistance), 0.5f);
    float borderMask = smoothstep(-aa, 0.0f, borderDistance) *
                       smoothstep(thickness + aa, thickness - aa, borderDistance);
    float opaqueWidth = min(thickness, 1.0f);
    float fadeWidth = max(thickness - opaqueWidth, 1.0f);
    float borderFade = saturate((borderDistance - opaqueWidth) / fadeWidth);
    float borderAlpha = lerp(1.0f, settings.outerAlpha, borderFade);

    float glowDistance = max(borderDistance - thickness, 0.0f);
    float glowMask = smoothstep(thickness - aa, thickness + aa, borderDistance);
    float glow = exp(-glowDistance * max(settings.glowFalloff, 0.001f)) *
                 glowMask *
                 settings.outerAlpha;

    float alpha = saturate(color.a * (borderMask * borderAlpha + glow));
    return float4(color.rgb * alpha, alpha);
}
