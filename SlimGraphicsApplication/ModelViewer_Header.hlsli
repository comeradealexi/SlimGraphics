#include "ShaderSharedStructures.h"

#ifdef __INTELLISENSE__
#define MESH_SHADER
#define VERTEX_TRIANGLE
#define VERTEX_QUAD
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
#define UAV_INDEX_MESH_SHADER_CULL_COUNT 2
#define UAV_INDEX_MESH_SHADER_PRIM_COUNT 3
#define UAV_INDEX_VERTEX_SHADER_INVOCATIONS 4

RWStructuredBuffer<uint> uav0 : register(u0);

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


[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void MSMain(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out indices uint3 tris[124],
    out vertices PS_INPUT verts[64]
)
{
    uint original;
    InterlockedAdd(uav0[UAV_INDEX_MESH_SHADER_INVOCATIONS], 1, original);
    
    {
    
        MeshletCullData cull_data = MeshletCullingDatas[gid];
        
        bool reject = false; // TODO - TAKE MODEL MATRIX IN TO ACCOUNT!
        
        if (model.meshlet_culling[0] != 0)
        {      
            if (dot(normalize(cull_data.cone_apex_xyz.xyz - camera.camera_position.xyz), cull_data.cone_axis_xyz_cone_cutoff_w.xyz) >= cull_data.cone_axis_xyz_cone_cutoff_w.w)
            {
                uint original;
                InterlockedAdd(uav0[UAV_INDEX_MESH_SHADER_CULL_COUNT], 1, original);
                reject = true;
            }
        }
        
        Meshlet m = Meshlets[gid];
        SetMeshOutputCounts(reject ? 0 : m.VertCount, reject ? 0 : m.PrimCount);
        
        if (reject)
        {
            return;
        }
        
        uint original;
        InterlockedAdd(uav0[UAV_INDEX_MESH_SHADER_PRIM_COUNT], m.PrimCount, original);
#if 0
    SetMeshOutputCounts(4 * 10, 3*10);
    if (gtid < m.VertCount)
    {
        uint vertexIndex = UniqueVertexIndices[m.VertOffset + gtid];
        Vertex v = Vertices[vertexIndex];
        float4 outpos;
        outpos = mul(float4(v.position, 1.0f), model.model_matrix);
        outpos = mul(outpos, camera.view_projection_matrix);
        
        verts[gtid].position = outpos;
        verts[gtid].colour = v.colour;
    }
    if (gtid < 3)
    {
        uint offset = m.PrimOffset + (gtid * 3);
        tris[gtid] = uint3(PrimitiveIndices.Load(offset + 0), PrimitiveIndices.Load(offset + 0), PrimitiveIndices.Load(offset + 0));
    }
#else

       // SetMeshOutputCounts(m.VertCount, m.PrimCount);

            if (gtid < m.VertCount)
            {
            //verts[gtid].position = float4(0, 0, 0, 0);
            //verts[gtid].colour = float3(0, 0, 0);
#if 1
                uint offset = m.VertOffset + gtid;
                uint size, num;
                UniqueVertexIndices.GetDimensions(size, num);
            //if (offset >= size)
            //{
            //    verts[gtid].position = float4(0, 0, 0, 0);
            //    verts[gtid].colour = float3(0, 0, 0);
            //
            //}
            //else
            {
                //verts[gtid].position = float4(0, 0, 0, 0);
                //verts[gtid].colour = float3(0, 0, 0);
                //verts[gtid].MeshletIndex = gid;

#if 1
                    uint vsize, vnum;
                    Vertices.GetDimensions(vsize, vnum);
                    uint vertexIndex = UniqueVertexIndices[offset];
                //if (vertexIndex >= vsize)
                //{
                //    verts[gtid].position = float4(0, 0, 0, 0);
                //    verts[gtid].colour = float3(0, 0, 0);
                //}
                //else
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
#endif
                }
#endif
            }

        if (gtid < m.PrimCount)
        {
            uint offset = m.PrimOffset + gtid;
            uint size, num;
            PrimitiveIndices.GetDimensions(size, num);

            //if (offset >= size)
            //{
            //    tris[gtid] = uint3(0, 0, 0);
            //
            //}
            //else
            {
                uint packedIndices = PrimitiveIndices[offset];
                tris[gtid] = uint3(packedIndices & 0xFF, (packedIndices >> 8) & 0xFF, (packedIndices >> 16) & 0xFF);
            }
            //tris[gtid] = uint3(0, 0, 0);
        }
#endif
    }
}

#endif

#ifdef VERTEX_TRIANGLE
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

float4 PSMain(PS_INPUT input 
#ifndef MESH_SHADER
, uint vid : SV_PrimitiveID
#endif
) : SV_TARGET
{
#ifdef MESH_SHADER
    uint vid = 0; // Not supported for mesh pipelines
#endif
    
    if (model.shading_mode == SHADING_MODE_MESHLETORDER)
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
        
        return float4(1.0 - smoothstep(0.0f, cull_data.cone_axis_xyz_cone_cutoff_w.w, dot(normalize(cull_data.cone_apex_xyz.xyz - camera.camera_position.xyz), cull_data.cone_axis_xyz_cone_cutoff_w.xyz)).x, 0.0, 0.0, 1.0);
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
    
    if (model.shading_mode != SHADING_MODE_PIXELORDER)
    {
        uint original;
        InterlockedAdd(uav0[UAV_INDEX_PIXELS_SHADED], 1, original);
    }
    
    return float4(input.colour.xyz, 1) * abs(dot(float3(0,0,1), input.normals));
}