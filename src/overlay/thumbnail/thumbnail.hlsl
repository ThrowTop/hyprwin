cbuffer ThumbnailConstants : register(b0) {
    float2 canvasSize;
    float2 rectPosition;
    float2 rectSize;
    float cornerRadius;
    float padding;
};

Texture2D thumbnailTexture : register(t0);
SamplerState thumbnailSampler : register(s0);

struct VSOut {
    float4 position : SV_Position;
};

VSOut vs_main(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut output;
    output.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float4 ps_main(VSOut input) : SV_Target {
    float2 local = input.position.xy - rectPosition;
    if (any(local < 0.0f) || any(local >= rectSize)) {
        discard;
    }

    float2 halfSize = rectSize * 0.5f;
    float radius = min(max(cornerRadius, 0.0f), min(halfSize.x, halfSize.y));
    float2 q = abs(local - halfSize) - halfSize + radius;
    float distance = length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - radius;
    float coverage = saturate(0.5f - distance / max(fwidth(distance), 0.5f));
    if (coverage <= 0.0f) {
        discard;
    }

    float4 color = thumbnailTexture.Sample(thumbnailSampler, local / rectSize);
    return float4(color.rgb * coverage, color.a * coverage);
}
