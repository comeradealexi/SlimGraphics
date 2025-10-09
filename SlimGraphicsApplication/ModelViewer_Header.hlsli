#include "ShaderSharedStructures.h"

#ifdef __INTELLISENSE__
#define MESH_SHADER
#define VERTEX_TRIANGLE
#define VERTEX_QUAD
#define MESH_AMP_SHADER
#define MODEL_VIEWER_SIMPLIFIED
#endif

cbuffer ConstantBufferData : register(b0)
{
    CameraData camera;
};

cbuffer ModelBufferData : register(b1)
{
    ModelData model;
};

#define UAV_INDEX_PIXELS_SHADED 0
#define UAV_INDEX_MESH_SHADER_INVOCATIONS 1
#define UAV_INDEX_MESH_SHADER_CULL_CONE_COUNT 2
#define UAV_INDEX_MESH_SHADER_PRIM_COUNT 3
#define UAV_INDEX_VERTEX_SHADER_INVOCATIONS 4
#define UAV_INDEX_MESH_SHADER_CULL_SPHERE_COUNT 5
#define UAV_INDEX_WAVE_INTRINSIC_COUNTER 6
#define UAV_INDEX_AMPLIFICATION_SHADER_INVOCATIONS 7

struct VS_INPUT
{
    float3 position : POSITION;
    float3 colour : COLOR0;
    float2 uvs : TEXCOORD0;
    float3 normals : NORMAL;
};
struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 colour : COLOR0;
    float2 uvs : TEXCOORD0;
    float3 normals : NORMAL;
    
#ifdef MESH_SHADER
    uint MeshletIndex : COLOR1;
#endif
};

#ifdef MODEL_VIEWER_SIMPLIFIED
PS_INPUT VSMain(VS_INPUT vs_in, uint id : SV_VertexID)
{
    PS_INPUT result;
    
    result.position = mul(float4(vs_in.position, 1.0f), model.model_matrix);
    result.position = mul(result.position, camera.view_projection_matrix);

    result.normals = normalize(mul(vs_in.normals, (float3x3) model.model_matrix));

    result.colour = vs_in.colour;
    result.uvs = vs_in.uvs;

    if (model.shading_mode == SHADING_MODE_VERTEXORDER)
    {
        float total_prims = (model.primitive_count * 3) * model.vertex_shading_mod;
        result.colour = (((float) id) % total_prims) / total_prims;
    }
    
    return result;
}
#ifdef FORCE_EARLY_DEPTH_STENCIL
[earlydepthstencil]
#endif
float4 PSMain(PS_INPUT input) : SV_TARGET
{
#ifdef MODEL_VIEWER_SIMPLIFIED_PIXEL_SHADER_DISCARD
    int idx_x = (int)((float)input.position.x) % 16;
    int idx_y = (int)((float)input.position.y) % 16;
    if (idx_x < (int)(((float)model.simplified_shading.x) * 16.0)) discard;
    if (idx_y < (int)(((float)model.simplified_shading.x) * 16.0)) discard;
#endif
    
    return float4(input.colour.xyz, 1) * abs(dot(float3(0, 0, 1), input.normals));
}

#else
RWStructuredBuffer<uint> uav0 : register(u0);
#ifdef MESH_SHADER
struct Vertex
{
    float3 position;
    float3 colour;
    float2 uvs;
    float3 normals;
    float3 tangents;
};

struct Meshlet
{
    uint VertOffset;
    uint PrimOffset;
    uint VertCount;
    uint PrimCount;
};

StructuredBuffer<Vertex>    Vertices                    : register(t0);
StructuredBuffer<Meshlet>   Meshlets                    : register(t1);
StructuredBuffer<uint>      UniqueVertexIndices         : register(t2);
StructuredBuffer<uint>      PrimitiveIndices            : register(t3);
StructuredBuffer<MeshletCullData> MeshletCullingDatas   : register(t4);

// Computes visiblity of an instance
// Performs a simple world-space bounding sphere vs. frustum plane check.
bool IsVisible(float4 boundingSphere)
{
    float4 center = float4(boundingSphere.xyz, 1.0);
    float radius = boundingSphere.w;

    for (int i = 0; i < 6; ++i)
    {
        if (dot(center, camera.Planes[i]) < -radius)
        {
            return false;
        }
    }

    return true;
}

bool IsVisibleFrustum(uint gid)
{
    MeshletCullData cull_data = MeshletCullingDatas[gid];
    float4 sphere_pos = mul(float4(cull_data.spherepos_xyz_radius_w.xyz, 1.0), model.model_matrix);
    float model_matrix_highest_scale = max(max(model.model_matrix[0][0], model.model_matrix[1][1]), model.model_matrix[2][2]);
    sphere_pos.w = cull_data.spherepos_xyz_radius_w.w * model_matrix_highest_scale;
    if (IsVisible(sphere_pos) == false)
    {
        uint original;
        InterlockedAdd(uav0[UAV_INDEX_MESH_SHADER_CULL_SPHERE_COUNT], 1, original);
        return false;
    }
    return true;
}

bool IsVisibleNormalCone(uint gid)
{
    MeshletCullData cull_data = MeshletCullingDatas[gid];

    float3 cone_apex = mul(float4(cull_data.cone_apex_xyz.xyz, 1.0f), model.model_matrix).xyz;
    float3 cone_axis = normalize(mul(cull_data.cone_axis_xyz_cone_cutoff_w.xyz, (float3x3) model.model_matrix));

    if (dot(normalize(cone_apex - camera.camera_position.xyz), cone_axis) >= cull_data.cone_axis_xyz_cone_cutoff_w.w)
    {
        uint original;
        InterlockedAdd(uav0[UAV_INDEX_MESH_SHADER_CULL_CONE_COUNT], 1, original);
        return false;
    }
    return true;
}

#ifdef MESH_AMP_SHADER
#define THREADS_PER_WAVE 32
#define AS_GROUP_SIZE THREADS_PER_WAVE
struct Payload
{
    uint MeshletIndices[AS_GROUP_SIZE];
    uint AmplificationID;
};
// The groupshared payload data to export to dispatched mesh shader threadgroups
groupshared Payload s_Payload;

[NumThreads(AS_GROUP_SIZE, 1, 1)]
void ASMain(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID)
{
    {    
        uint original;
        InterlockedAdd(uav0[UAV_INDEX_AMPLIFICATION_SHADER_INVOCATIONS], 1, original);
    }

    bool visible = true;

    // Check bounds of meshlet cull data resource
    if (dtid < model.meshlet_count)
    {        
        if (visible && model.meshlet_culling.y != 0)
        {
            visible = IsVisibleFrustum(dtid);

        }
        
        if (visible && model.meshlet_culling.x != 0)
        {
            visible = IsVisibleNormalCone(dtid);
        }
    }
    
    if (gtid == 0)
    {
        s_Payload.AmplificationID = (gid + 1) * 6; // To make the 0th element black colour
    }
    
    // Compact visible meshlets into the export payload array
    if (visible)
    {
        uint index = WavePrefixCountBits(visible);
        s_Payload.MeshletIndices[index] = dtid;
    }
    
    // Dispatch the required number of MS threadgroups to render the visible meshlets
    uint visibleCount = WaveActiveCountBits(visible);
    DispatchMesh(visibleCount, 1, 1, s_Payload);
}
#endif

#ifdef MESH_COMPUTE_CULL
#define THREADS_PER_WAVE 128
groupshared unsigned int shared_data[THREADS_PER_WAVE];

[numthreads(THREADS_PER_WAVE, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    
}
#endif


[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void MSMain(
    uint gtid : SV_GroupThreadID
    ,uint gid : SV_GroupID
#ifdef MESH_AMP_SHADER
, in payload Payload payload
#endif
    ,out indices uint3 tris[124]
   , out vertices PS_INPUT verts[64]

)
{ 
    {    
        uint original;
        InterlockedAdd(uav0[UAV_INDEX_MESH_SHADER_INVOCATIONS], 1, original);
    }
    

#ifdef MESH_AMP_SHADER 
    // Load the meshlet from the AS payload data
    uint meshletIndex = payload.MeshletIndices[gid];

    bool reject = false;
    
    // Catch any out-of-range indices (in case too many MS threadgroups were dispatched from AS)
    if (meshletIndex >= model.meshlet_count)
    {
        reject = true;
    }
    // Load the meshlet
    Meshlet m = Meshlets[meshletIndex];
    
    // Our vertex and primitive counts come directly from the meshlet
    SetMeshOutputCounts(reject ? 0 : m.VertCount, reject ? 0 : m.PrimCount);
    
    if (reject)
    {
        return;
    }
    
    uint original;
    InterlockedAdd(uav0[UAV_INDEX_MESH_SHADER_PRIM_COUNT], m.PrimCount, original);
        
    if (gtid < m.VertCount)
    {
        uint offset = m.VertOffset + gtid;
        {
            uint vertexIndex = model.meshlet_vb_offset + UniqueVertexIndices[offset];
            {
                Vertex v = Vertices[vertexIndex];
                float4 outpos;
                outpos = mul(float4(v.position, 1.0f), model.model_matrix);
                outpos = mul(outpos, camera.view_projection_matrix);
                
                float3 outnormals;
                outnormals = normalize(mul(v.normals, (float3x3) model.model_matrix));
        
                verts[gtid].position = outpos;
                verts[gtid].colour = v.colour;
                verts[gtid].uvs = v.uvs;
                verts[gtid].normals = outnormals;
                
                if (model.shading_mode == SHADING_MODE_AMPLIFICATION_ORDER)
                {
                    verts[gtid].MeshletIndex = payload.AmplificationID;
                }
                else
                {
                    verts[gtid].MeshletIndex = gid;
                }              
            }
        }
    }

    if (gtid < m.PrimCount)
    {
        uint offset = m.PrimOffset + gtid;
            {
            uint packedIndices = PrimitiveIndices[offset];
            tris[gtid] = uint3(packedIndices & 0xFF, (packedIndices >> 8) & 0xFF, (packedIndices >> 16) & 0xFF);
        }
    }
#else 
        
        {        
    
        MeshletCullData cull_data = MeshletCullingDatas[gid];
        
        bool reject = false; // TODO - TAKE MODEL MATRIX IN TO ACCOUNT!
        
        if (!reject && model.meshlet_culling.y != 0)
        {              
            reject = IsVisibleFrustum(gid) == false;

        }
        
        if (!reject && model.meshlet_culling.x != 0)
        {      
            reject = IsVisibleNormalCone(gid) == false;

        }
        
        Meshlet m = Meshlets[gid];
        SetMeshOutputCounts(reject ? 0 : m.VertCount, reject ? 0 : m.PrimCount);
        
        if (reject)
        {
            return;
        }
        
        uint original;
        InterlockedAdd(uav0[UAV_INDEX_MESH_SHADER_PRIM_COUNT], m.PrimCount, original);

        if (gtid < m.VertCount)
        {
            uint offset = m.VertOffset + gtid;
        {
                uint vertexIndex = model.meshlet_vb_offset + UniqueVertexIndices[offset];
            {
                    Vertex v = Vertices[vertexIndex];
                    float4 outpos;
                    outpos = mul(float4(v.position, 1.0f), model.model_matrix);
                    outpos = mul(outpos, camera.view_projection_matrix);
                
                    float3 outnormals;
                    outnormals = normalize(mul(v.normals, (float3x3) model.model_matrix));
        
                    verts[gtid].position = outpos;
                    verts[gtid].colour = v.colour;
                    verts[gtid].uvs = v.uvs;
                    verts[gtid].normals = outnormals;
                    verts[gtid].MeshletIndex = gid;

                }
            }
        }

        if (gtid < m.PrimCount)
        {
            uint offset = m.PrimOffset + gtid;
            {
                uint packedIndices = PrimitiveIndices[offset];
                tris[gtid] = uint3(packedIndices & 0xFF, (packedIndices >> 8) & 0xFF, (packedIndices >> 16) & 0xFF);
            }
        }
    }    
#endif
    }

#endif

#if defined(VERTEX_TRIANGLE) || defined(VERTEX_MIDDLE_TRIANGLE)
PS_INPUT VSMain(uint id : SV_VertexID)
{
    uint original;
    InterlockedAdd(uav0[UAV_INDEX_VERTEX_SHADER_INVOCATIONS], 1, original);
    
    PS_INPUT psOut;
    
    float2 uv = float2((id << 1) & 2, id & 2);
    //psOut.UVs = uv;
    psOut.colour = float3(0,0,0);
    psOut.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    psOut.uvs = uv;
    psOut.normals = float3(0, 0, -1);
    
    #ifdef VERTEX_MIDDLE_TRIANGLE
    if (id == 0)
	{
        psOut.position.x = 0.0f;
        psOut.position.y = 0.75f;
    }
    else if (id == 1)
	{
        psOut.position.x = 0.75f;
        psOut.position.y = -0.75f;
    }
    else if (id == 2)
	{
        psOut.position.x = -0.75f;
        psOut.position.y = -0.75f;
    }
    #endif
    
    return psOut;

}
#elif defined (VERTEX_QUAD)
PS_INPUT VSMain(uint id : SV_VertexID)
{
    uint original;
    InterlockedAdd(uav0[UAV_INDEX_VERTEX_SHADER_INVOCATIONS], 1, original);

    PS_INPUT psOut;
	//psOut.Normal = float3(0, 1, 0);
    psOut.colour = float3(0,0,0);
    psOut.uvs = float2(0,0);
    psOut.normals = float3(0,0,-1);

	if (id == 1)
	{
		psOut.position = float4(-1, -1, 0, 1);
		//psOut.UVs = float2(0, 0);
	}
	else if (id == 0)
	{
		psOut.position = float4(1, -1, 0, 1);
		//psOut.UVs = float2(1, 0);
	}
	else if (id == 2)
	{
		psOut.position = float4(1, 1, 0, 1);
		//psOut.UVs = float2(1, 1);
	}
	else if (id == 3)
	{
		psOut.position = float4(1, 1, 0, 1);
		//psOut.UVs = float2(1, 1);
	}
	else if (id == 4)
	{
		psOut.position = float4(-1, -1, 0, 1);
		//psOut.UVs = float2(0, 0);
	}
	else //id = 5
	{
		psOut.position = float4(-1, 1, 0, 1);
		//psOut.UVs = float2(0, 1);
	}
    return psOut;

}
#else

PS_INPUT VSMain(VS_INPUT vs_in, uint id : SV_VertexID)
{
    uint original;
    InterlockedAdd(uav0[UAV_INDEX_VERTEX_SHADER_INVOCATIONS], 1, original);

    PS_INPUT result;
    
    result.position = mul(float4(vs_in.position, 1.0f), model.model_matrix);
    result.position = mul(result.position, camera.view_projection_matrix);

    result.normals = normalize(mul(vs_in.normals, (float3x3)model.model_matrix));

    result.colour = vs_in.colour;
    result.uvs = vs_in.uvs;

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
    InterlockedAdd(uav0[UAV_INDEX_PIXELS_SHADED], 1, original);
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

//[WaveSize(64)]
#ifdef FORCE_EARLY_DEPTH_STENCIL
[earlydepthstencil]
#endif
float4 PSMain(PS_INPUT input 
#ifndef MESH_SHADER
, uint vid : SV_PrimitiveID
#endif
) : SV_TARGET
{
#ifdef MESH_SHADER
    uint vid = 0; // Not supported for mesh pipelines
#endif
    
    if (model.shading_mode != SHADING_MODE_PIXELORDER)
    {
        uint original;
        InterlockedAdd(uav0[UAV_INDEX_PIXELS_SHADED], 1, original);
    }
    
    if (model.shading_mode == SHADING_MODE_MESHLETORDER || model.shading_mode == SHADING_MODE_AMPLIFICATION_ORDER)
    {
     #ifdef MESH_SHADER
        uint meshletIndex = input.MeshletIndex;

        //float3 diffuseColor = float3(
        //    float(meshletIndex & 1),
        //    float(meshletIndex & 3) / 4,
        //    float(meshletIndex & 7) / 8); 
        
        //float3 diffuseColor = float3(
        //    float(meshletIndex & 3) / 4,
        //    float(meshletIndex & 7) / 8,
        //    float(meshletIndex & 15) / 16);
        
            float3 diffuseColor = float3(
            float(meshletIndex & 7) / 8,
            float(meshletIndex & 15) / 16,
            float(meshletIndex & 31) / 32);
        
        //float col = ((float) input.MeshletIndex) / ((float) model.meshlet_count);
        return float4(diffuseColor, 1);       
#else
        return float4(1.0, 0.5,0.5, 1.0);
#endif
    }
    
    if (model.shading_mode == SHADING_MODE_MESHLET_CULL_ANGLE)
    {
 #ifdef MESH_SHADER
        // TODO - TAKE MODEL MATRIX IN TO ACCOUNT!
        uint meshletIndex = input.MeshletIndex;
        MeshletCullData cull_data = MeshletCullingDatas[meshletIndex];
        
        // Meshlet wil always be rendered.
        if (cull_data.cone_axis_xyz_cone_cutoff_w.w == 1.0)
        {
            return float4(0.0f, 0.7f, 0.0f, 1.0f);
        }
        
        float3 cone_apex = mul(float4(cull_data.cone_apex_xyz.xyz, 1.0f), model.model_matrix).xyz;
        float3 cone_axis = normalize(mul(cull_data.cone_axis_xyz_cone_cutoff_w.xyz, (float3x3) model.model_matrix));
        
        return float4(1.0 - smoothstep(0.0f, cull_data.cone_axis_xyz_cone_cutoff_w.w, dot(normalize(cone_apex - camera.camera_position.xyz), cone_axis)).x, 0.0, 0.0, 1.0);
#else
        return float4(1.0, 0.5,0.5, 1.0);
#endif
    }
    
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
    else if (model.shading_mode == SHADING_MODE_WAVE_INTRINSICS)
    {
        uint max_lane_size = model.wave_intrinsics.x;
        uint laneCount = WaveGetLaneCount();      
        
        if (model.wave_intrinsics.y == 0)
        {
            return float4((WaveGetLaneIndex() / float(laneCount)).xxx, 1.0);
        }
        else if (model.wave_intrinsics.y == 1)
        {
            float4 outputColor;
            if (WaveIsFirstLane())
            {
                uint original;
                InterlockedAdd(uav0[UAV_INDEX_WAVE_INTRINSIC_COUNTER], 1, original);
                
                float w = camera.screen_dimensions_and_depth_info.x;
                float h = camera.screen_dimensions_and_depth_info.y;
                float total_pixels = ((float) w * (float) h);
                
                float col = float(original) / (total_pixels / float(laneCount));
                outputColor = float4(col.xxx, 1.0);
            }
            
            outputColor = WaveReadLaneFirst(outputColor);

            return outputColor;
        }
        else if (model.wave_intrinsics.y == 2)
        {
            // Example of vote intrinsics: WaveActiveBallot
            // Active lanes ratios (# of total activelanes / # of total lanes).
            uint4 activeLaneMask = WaveActiveBallot(true);
            uint numActiveLanes = countbits(activeLaneMask.x) + countbits(activeLaneMask.y) + countbits(activeLaneMask.z) + countbits(activeLaneMask.w);
            float activeRatio = (float) numActiveLanes / float(laneCount);
            if (activeRatio >= 1.0)
            {
                return 1.0.xxxx;
            }
            return float4(activeRatio, 0, 0, 1.0);
        }
        else if (model.wave_intrinsics.y == 3)
        {
            float fcol = 1.0f;
            if (IsHelperLane())
            {
                fcol = 0.0f;
            }
            float ddx_result = ddx(fcol);
            
            if (ddx_result <= 0.0f)
            {
                return float4(1, 1, 1, 1);
            }
            else
            {
                uint original;
                InterlockedAdd(uav0[UAV_INDEX_WAVE_INTRINSIC_COUNTER], 1, original);
                return float4(1, 0, 0, 1);
            }                      
            }
        else if (model.wave_intrinsics.y == 4)
        {
            return float4((laneCount / max_lane_size).xxx, 1);
        }
        else if (model.wave_intrinsics.y == 5)
        {
            // Checks to ensure areas around waves such as edge of triangles behave as expected when there are not full waves
            bool is_left = input.position.x < (camera.screen_dimensions_and_depth_info.x * 0.5);
            bool all_waves_same = WaveActiveAllTrue(is_left);
            if (all_waves_same)
            {
                return is_left ? float4(0, 0, 1, 1) : float4(0, 1, 0, 1);
            }
            else
            {
                return float4(1, 0, 0, 1);
            }
        }
    }
    
    return float4(input.colour.xyz, 1) * abs(dot(float3(0,0,1), input.normals));
}
#endif