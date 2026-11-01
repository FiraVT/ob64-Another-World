// RmlUi geometry (app/features/rml_ui.cpp). Positions are in window pixels; the push
// constants carry RmlUi's per-draw translation and 2 / screen size.
// RT64's DXC options add -fvk-invert-y for SPIR-V, so this is written in the
// D3D convention (y down in pixels, y up in clip space) for every backend.

struct RmlUiCB {
    float2 translation;
    float2 inverseHalfSize;
};

[[vk::push_constant]] ConstantBuffer<RmlUiCB> gConstants : register(b0);

void VSMain(in float2 position : POSITION, in float4 color : COLOR, in float2 uv : TEXCOORD,
            out float4 pos : SV_Position, out float4 outColor : COLOR, out float2 outUv : TEXCOORD) {
    const float2 pixel = position + gConstants.translation;
    const float2 ndc = pixel * gConstants.inverseHalfSize - 1.0f;
    pos = float4(ndc.x, -ndc.y, 0.0f, 1.0f);
    outColor = color;
    outUv = uv;
}

