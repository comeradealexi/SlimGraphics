#include "ShaderSharedStructures.h"

cbuffer ConstantBufferData : register(b0)
{
    CameraData camera;
};

cbuffer ModelBufferData : register(b1)
{
    ModelData model;
};

RWStructuredBuffer<uint> uav0 : register(u0);

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

#ifdef VERTEX_TRIANGLE
PS_INPUT VSMain(uint id : SV_VertexID)
{
    PS_INPUT psOut;
    
    float2 uv = float2((id << 1) & 2, id & 2);
    //psOut.UVs = uv;
    psOut.colour = float3(0,0,0);
    psOut.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    
    return psOut;

}
#elif defined (VERTEX_QUAD)
PS_INPUT VSMain(uint id : SV_VertexID)
{
    PS_INPUT psOut;
	//psOut.Normal = float3(0, 1, 0);
    psOut.colour = float3(0,0,0);
	if (id == 1)
	{
		psOut.position = float4(-1, -1, 1, 1);
		//psOut.UVs = float2(0, 0);
	}
	else if (id == 0)
	{
		psOut.position = float4(1, -1, 1, 1);
		//psOut.UVs = float2(1, 0);
	}
	else if (id == 2)
	{
		psOut.position = float4(1, 1, 1, 1);
		//psOut.UVs = float2(1, 1);
	}
	else if (id == 3)
	{
		psOut.position = float4(1, 1, 1, 1);
		//psOut.UVs = float2(1, 1);
	}
	else if (id == 4)
	{
		psOut.position = float4(-1, -1, 1, 1);
		//psOut.UVs = float2(0, 0);
	}
	else //id = 5
	{
		psOut.position = float4(-1, 1, 1, 1);
		//psOut.UVs = float2(0, 1);
	}
    return psOut;

}
#else

PS_INPUT VSMain(VS_INPUT vs_in, uint id : SV_VertexID)
{
    PS_INPUT result;
    
    result.position = mul(float4(vs_in.position, 1.0f), model.model_matrix);
    result.position = mul(result.position, camera.view_projection_matrix);

    result.colour = vs_in.colour;
    
    if (model.shading_mode == SHADING_MODE_VERTEXORDER)
    {
        float total_prims = (model.primitive_count * 3) * model.vertex_shading_mod;
        result.colour = (((float) id) % total_prims) / total_prims;
    }
    
    return result;
}
#endif

float4 ShadePixelOrder() : SV_TARGET0
{
    uint true_original;
    uint original;
    InterlockedAdd(uav0[0], 1, original);
    true_original = original;
    float w = camera.screen_dimensions_and_depth_info.x;
    float h = camera.screen_dimensions_and_depth_info.y;
    float total_pixels = ((float) w * (float)h) * model.pixel_order_data1.y;
    
    uint mod_idx = (uint) (((float) original) / total_pixels);
    mod_idx = mod_idx % 3;
    

    original = original % (uint) total_pixels;
    
    float outv = ((float) original) / total_pixels;
    outv = 1.0 - outv;
    
    float4 fout = float4((outv * model.pixel_order_data1.x).xxx, 1);
    
    if (model.pixel_order_data1.w)
    {
        float absolute = abs(((float) true_original) - model.pixel_order_data2.x);
        if (absolute <= model.pixel_order_data2.y)
        {
            return float4(1, 0, 0, 1);
        }
        
        return fout;
    }
    
    if (model.pixel_order_data1.z != 0.0f)
    {
        if (mod_idx == 0)
        {
            return fout * float4(1, 0, 0, 1);
        }
        if (mod_idx == 1)
        {
            return fout * float4(0, 1, 0, 1);
        }
        if (mod_idx == 2)
        {
            return fout * float4(0, 0, 1, 1);
        }
    }
    return fout;

}

float4 PSMain(PS_INPUT input, uint vid : SV_PrimitiveID) : SV_TARGET
{
    if (model.shading_mode == SHADING_MODE_PRIMITIVEORDER)
    {
        float vid_f = vid;
        float total_prims = (model.primitive_count) * model.vertex_shading_mod;
        float col = (vid_f % model.primitive_count) / total_prims;
        return float4(col.xxx, 1);
    }
    else if (model.shading_mode == SHADING_MODE_VERTEXORDER)
    {
        return float4(input.colour, 1);
    }
    else if (model.shading_mode == SHADING_MODE_PIXELORDER)
    {
        return ShadePixelOrder();
    }
    
        return float4(input.colour.xyz, 1);
}

/*
#include "OffScreenShared.h"

RWStructuredBuffer<uint> uav0 : register(u1);

cbuffer ShadeCB : register(b1)
{
    float4 scale; //x=multiplier, y=mod, z=use_colours, w=shade_over_time
    float4 time;  //x=pixel_to_shade, y=colour_range
}

float4 main(PS_INPUT input) : SV_TARGET0
{
    uint true_original;
    uint original;
    InterlockedAdd(uav0[0], 1, original);
    true_original = original;
    float total_pixels = (1280.0 * 720.0) * scale.y;
    
    uint mod_idx = (uint) (((float) original) / total_pixels);
    mod_idx = mod_idx % 3;
    

    original = original % (uint) total_pixels;
    
    float outv = ((float) original) / total_pixels;
    outv = 1.0 - outv;
    
    float4 fout = float4((outv * scale.x).xxx, 1);
    
    if (scale.w)
    {
        float absolute = abs(((float) true_original) - time.x);
        if (absolute <= time.y)
        {
            return float4(1, 0, 0, 1);
        }
        
        return fout;
    }
    
    if (scale.z != 0.0f)
    {
        if (mod_idx == 0)
        {
            return fout * float4(1, 0, 0, 1);
        }
        if (mod_idx == 1)
        {
            return fout * float4(0, 1, 0, 1);
        }
        if (mod_idx == 2)
        {
            return fout * float4(0, 0, 1, 1);
        }
    }
    return fout;

}


*/