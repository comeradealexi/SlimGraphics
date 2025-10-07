// Normally argument "dipatchGridDim" is parsed through a constant buffer. However, if for some reason it is a
// static value, some DXC compiler versions will be unable to compile the code.
// If that's the case for you, flip DXC_STATIC_DISPATCH_GRID_DIM definition from 0 to 1.
#define DXC_STATIC_DISPATCH_GRID_DIM 1

// Divide the 2D-Dispatch_Grid into tiles of dimension [N, DipatchGridDim.y]
// “CTA” (Cooperative Thread Array) == Thread Group in DirectX terminology
uint2 ThreadGroupTilingX(
	const uint2 dipatchGridDim, // Arguments of the Dispatch call (typically from a ConstantBuffer)
	const uint2 ctaDim, // Already known in HLSL, eg:[numthreads(8, 8, 1)] -> uint2(8, 8)
	const uint maxTileWidth, // User parameter (N). Recommended values: 8, 16 or 32.
	const uint2 groupThreadID, // SV_GroupThreadID
	const uint2 groupId // SV_GroupID
)
{
	// A perfect tile is one with dimensions = [maxTileWidth, dipatchGridDim.y]
    const uint Number_of_CTAs_in_a_perfect_tile = maxTileWidth * dipatchGridDim.y;

	// Possible number of perfect tiles
    const uint Number_of_perfect_tiles = dipatchGridDim.x / maxTileWidth;

	// Total number of CTAs present in the perfect tiles
    const uint Total_CTAs_in_all_perfect_tiles = Number_of_perfect_tiles * maxTileWidth * dipatchGridDim.y;
    const uint vThreadGroupIDFlattened = dipatchGridDim.x * groupId.y + groupId.x;

	// Tile_ID_of_current_CTA : current CTA to TILE-ID mapping.
    const uint Tile_ID_of_current_CTA = vThreadGroupIDFlattened / Number_of_CTAs_in_a_perfect_tile;
    const uint Local_CTA_ID_within_current_tile = vThreadGroupIDFlattened % Number_of_CTAs_in_a_perfect_tile;
    uint Local_CTA_ID_y_within_current_tile;
    uint Local_CTA_ID_x_within_current_tile;

    if (Total_CTAs_in_all_perfect_tiles <= vThreadGroupIDFlattened)
    {
		// Path taken only if the last tile has imperfect dimensions and CTAs from the last tile are launched. 
        uint X_dimension_of_last_tile = dipatchGridDim.x % maxTileWidth;
#ifdef DXC_STATIC_DISPATCH_GRID_DIM
        X_dimension_of_last_tile = max(1, X_dimension_of_last_tile);
#endif
        Local_CTA_ID_y_within_current_tile = Local_CTA_ID_within_current_tile / X_dimension_of_last_tile;
        Local_CTA_ID_x_within_current_tile = Local_CTA_ID_within_current_tile % X_dimension_of_last_tile;
    }
    else
    {
        Local_CTA_ID_y_within_current_tile = Local_CTA_ID_within_current_tile / maxTileWidth;
        Local_CTA_ID_x_within_current_tile = Local_CTA_ID_within_current_tile % maxTileWidth;
    }

    const uint Swizzled_vThreadGroupIDFlattened =
		Tile_ID_of_current_CTA * maxTileWidth +
		Local_CTA_ID_y_within_current_tile * dipatchGridDim.x +
		Local_CTA_ID_x_within_current_tile;

    uint2 SwizzledvThreadGroupID;
    SwizzledvThreadGroupID.y = Swizzled_vThreadGroupIDFlattened / dipatchGridDim.x;
    SwizzledvThreadGroupID.x = Swizzled_vThreadGroupIDFlattened % dipatchGridDim.x;

    uint2 SwizzledvThreadID;
    SwizzledvThreadID.x = ctaDim.x * SwizzledvThreadGroupID.x + groupThreadID.x;
    SwizzledvThreadID.y = ctaDim.y * SwizzledvThreadGroupID.y + groupThreadID.y;

    return SwizzledvThreadID.xy;
}

#include "ShaderSharedStructures.h"

#ifndef THREAD_COUNT_X
#error Expecting THREAD_COUNT_X to be defined
#endif
#ifndef THREAD_COUNT_Y
#error Expecting THREAD_COUNT_Y to be defined
#endif

#define THREAD_COUNT_Z 1

cbuffer ConstantBufferData : register(b0)
{
    CameraData camera;
};

cbuffer PostProcessBufferData : register(b1)
{
    PostProcessData post_process_data;
};

// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-object-rwtexture2d
//The runtime enforces certain usage patterns when you create multiple view types to the same resource. 
// For example, the runtime does not allow you to have both a UAV mapping for a resource and SRV mapping for the same resource active at the same time.
// You can prefix RWTexture2D objects with the storage class globallycoherent. 
// This storage class causes memory barriers and syncs to flush data across the entire GPU such that other groups can see writes. 
// Without this specifier, a memory barrier or sync will flush only an unordered access view (UAV) within the current group.
RWTexture2D<float4>  out_tex : register(u0);

// Input textures
Texture2D in_tex_colour : register(t0);
Texture2D in_tex_depth : register(t1);

[numthreads(THREAD_COUNT_X, THREAD_COUNT_Y, THREAD_COUNT_Z)]
void main(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID, uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint3 original_dispatch_thread_id = dispatch_thread_id;
    
    if (post_process_data.optimisations.x != 0)
    {
        dispatch_thread_id.xy = ThreadGroupTilingX(uint2(post_process_data.mode.x, post_process_data.mode.y), uint2(THREAD_COUNT_X, THREAD_COUNT_Y), 32, group_thread_id, group_id);
        if (post_process_data.optimisations.y == false)
        {
            original_dispatch_thread_id.xy = dispatch_thread_id.xy;
        }
    }    
    
    if (dispatch_thread_id.x < camera.screen_dimensions_and_depth_info.x
        && dispatch_thread_id.y < camera.screen_dimensions_and_depth_info.y)
    {        
        float4 out_colour = 0.0.xxxx;
        if (post_process_data.mode.w == 0)
        {
            out_colour =  in_tex_colour.Load(int3(dispatch_thread_id.xy, 0));
        }
        else if (post_process_data.mode.w == 1)
        {
            out_colour = in_tex_depth.Load(int3(dispatch_thread_id.xy,0)).xxxx;
        }
        else if (post_process_data.mode.w == 2)
        {
            out_colour.x = ((float)(group_id.x)) / ((float)(post_process_data.mode.x));
            out_colour.y = ((float)(group_id.y)) / ((float)(post_process_data.mode.y));
            out_colour.z = ((float)(group_id.z)) / ((float)(post_process_data.mode.z));
        }
        else if (post_process_data.mode.w == 3)
        {
            out_colour.x = ((float)(group_thread_id.x)) / ((float)(THREAD_COUNT_X));
            out_colour.y = ((float)(group_thread_id.y)) / ((float)(THREAD_COUNT_Y));
            out_colour.z = ((float)(group_thread_id.z)) / ((float)(THREAD_COUNT_Z));
        }
        else if (post_process_data.mode.w == 4)
        {
            out_colour.x = ((float)(dispatch_thread_id.x)) / ((float) (THREAD_COUNT_X * post_process_data.mode.x));
            out_colour.y = ((float)(dispatch_thread_id.y)) / ((float) (THREAD_COUNT_Y * post_process_data.mode.y));
            out_colour.z = ((float)(dispatch_thread_id.z)) / ((float) (THREAD_COUNT_Z * post_process_data.mode.z));
        }
        
        out_colour = clamp(out_colour, post_process_data.colour_clamping.x.xxxx, post_process_data.colour_clamping.y.xxxx);
        if (post_process_data.colour_clamping.z > 0.0f)
        {
            float diff = post_process_data.colour_clamping.y - post_process_data.colour_clamping.x;
            out_colour -= post_process_data.colour_clamping.x.xxxx;
            out_colour /= diff.xxxx;
        }

        // Colour Bit Count
        if (post_process_data.colour_bit_values.x > 0)
        {
            int bits = pow(2, post_process_data.colour_bit_values.x) - 1;
            int4 colour_ints = int4(out_colour * float4(bits.xxxx)); // 0 - 255;
            out_colour = clamp(float4(colour_ints / float4(bits.xxxx)), 0.0.xxxx, 1.0.xxxx);
        }

        if (post_process_data.frac_output.x > 0.0f)
        {
            out_colour *= post_process_data.frac_output.y.xxxx;
            out_colour = frac(out_colour);
        }

        out_colour *= post_process_data.colour_output_enabled;
        out_tex[original_dispatch_thread_id.xy] = out_colour;
    }
}