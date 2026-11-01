// RmlUi geometry (app/features/rml_ui.cpp). The vertex colour is premultiplied, and
// untextured geometry binds a 1x1 white texture, so one shader serves both.

Texture2D<float4> gTexture : register(t1);
SamplerState gSampler : register(s2);

float4 PSMain(in float4 pos : SV_Position, in float4 color : COLOR, in float2 uv : TEXCOORD) : SV_Target {
    return gTexture.Sample(gSampler, uv) * color;
}

