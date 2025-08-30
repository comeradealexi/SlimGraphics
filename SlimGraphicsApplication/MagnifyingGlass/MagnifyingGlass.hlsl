#include "../ShaderSharedStructures.h"

Texture2D tex : register(t0);

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

    float2 sample_pixel = source_pixel_centre;
    sample_pixel.x -= (dest_texture_size.x * magnify_constant_data.scale.x) * 0.5;
    sample_pixel.y -= (dest_texture_size.y * magnify_constant_data.scale.x) * 0.5;

    sample_pixel.x += (input.uvs.x * magnify_constant_data.scale.x) * dest_texture_size.x;
    sample_pixel.y += (input.uvs.y * magnify_constant_data.scale.x) * dest_texture_size.y;

    return float4(tex.Load(int3(sample_pixel.x,sample_pixel.y,0)).rgb, 1.0);
}