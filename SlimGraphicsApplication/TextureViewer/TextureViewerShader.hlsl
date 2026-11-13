#include "../ShaderSharedStructures.h"

Texture2D tex : register(t0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uvs : TEXCOORD0;
};

PS_INPUT VSMain(uint id : SV_VertexID)
{
    PS_INPUT psOut;
    float2 uv = float2((id << 1) & 2, id & 2);
    psOut.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    psOut.uvs = uv;
    return psOut;
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    uint w;
    uint h;
    tex.GetDimensions(w, h);
    return float4(tex.Load(int3(input.uvs.x * (float) w, input.uvs.y * (float) h, 0)).rgb, 1.0);
}