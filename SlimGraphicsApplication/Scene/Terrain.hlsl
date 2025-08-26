#include "..\ShaderSharedStructures.h"

Texture2D<float4> terrain_texture : register(t0);

cbuffer ConstantBufferData : register(b0)
{
    CameraData camera;
};

cbuffer TerraintBufferData : register(b1)
{
    TerrainData terrain;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 colour : COLOR0;
    float2 uvs : TEXCOORD0;
    float3 normals : NORMAL;
};

PS_INPUT VSMain(uint id : SV_VertexID)
{
    
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    return (0.0).xxxx;
}