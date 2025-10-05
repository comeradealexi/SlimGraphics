#include "ShaderSharedStructures.h"

#ifndef THREAD_COUNT_X
#error Expecting THREAD_COUNT_X to be defined
#endif
#ifndef THREAD_COUNT_Y
#error Expecting THREAD_COUNT_Y to be defined
#endif

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

[numthreads(THREAD_COUNT_X, THREAD_COUNT_Y, 1)]
void main( uint3 dispatch_thread_id : SV_DispatchThreadID )
{
    if (dispatch_thread_id.x < camera.screen_dimensions_and_depth_info.x
        && dispatch_thread_id.y < camera.screen_dimensions_and_depth_info.y)
    {

        float4 out_colour = in_tex_colour.Load(int3(dispatch_thread_id.xy,0));
        out_colour *= post_process_data.colour_output_enabled;
        out_tex[dispatch_thread_id.xy] = out_colour;
    }
}