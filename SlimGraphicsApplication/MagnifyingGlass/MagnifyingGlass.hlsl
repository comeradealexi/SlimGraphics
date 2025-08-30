#include "../ShaderSharedStructures.h"

Texture2D texture : register(t0);
SamplerState sampler : register(s0);


cbuffer ConstantBufferData : register(b0)
{
    CameraData camera;
};

cbuffer MagnifyBufferData : register(b1)
{
    MagnifyingGlassData magnify_constant_data;
};

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
    float2 source_pixel_centre = magnify_constant_data.coordinates.xy;
    float2 dest_texture_size = magnify_constant_data.coordinates.zw;
}