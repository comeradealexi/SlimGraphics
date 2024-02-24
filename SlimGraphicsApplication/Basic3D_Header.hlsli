
cbuffer ConstantBufferData : register(b0)
{
    float4x4 model_view_projection_matrix;
};

struct VS_INPUT
{
    float3 position : POSITION;
    float3 colour : COLOR0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 colour : COLOR0;
};

PS_INPUT VSMain(VS_INPUT vs_in, uint id : SV_VertexID)
{
    PS_INPUT result;
    
    result.position = mul(float4(vs_in.position, 1.0f), model_view_projection_matrix);
    result.colour = vs_in.colour;
    
    return result;
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    return float4(input.colour.xyz, 1);
}
