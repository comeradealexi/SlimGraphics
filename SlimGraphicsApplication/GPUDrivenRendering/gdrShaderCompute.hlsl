#include "gdrShaderStructures.h"

struct Vertex
{
    float3 position;
    float3 colour;
    float2 uvs;
    float3 normals;
    float3 tangents;
};

// Read Buffers
StructuredBuffer<Vertex> Vertices : register(t0);
StructuredBuffer<MeshletData> Meshlets : register(t1);
StructuredBuffer<uint> UniqueVertexIndices : register(t2);
StructuredBuffer<uint> PrimitiveIndices : register(t3);
StructuredBuffer<InstanceData> Instances : register(t4);
//StructuredBuffer<ModelData> Models : register(t5);

// Write Buffers
RWStructuredBuffer<PerInstanceProcessingData_Output> PerInstanceDataOutput : register(u0);
RWStructuredBuffer<uint> AtomicCounter : register(u1);
RWStructuredBuffer<uint> IndirectArgumentBuffer : register(u2);

cbuffer ConstantBufferData : register(b0)
{
    CameraData camera;
};

cbuffer PerInstanceProcessingDataCBuffer : register(b1)
{
    PerInstanceProcessingData constant_data;
};

[numthreads(32, 1, 1)]
void per_instance_processing_main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint thread_id = dispatch_id.x;
    
    if (thread_id < constant_data.meshlets_to_process_count)
    {
        uint instance_index = constant_data.instance_offset + (thread_id / constant_data.meshlets_per_instance);
        uint meshlet_index = constant_data.meshlet_offset + (thread_id % constant_data.meshlets_per_instance);
        
        InstanceData instance = Instances[instance_index];
        MeshletData meshlet = Meshlets[meshlet_index];
        
        // TODO: Visibility and culling checks before adding to list
        bool visible = true;
        
        if (visible)
        {
            uint data_output_index;
            InterlockedAdd(AtomicCounter[0], 1, data_output_index);
            
            PerInstanceProcessingData_Output meshlet;
            meshlet.instance_index = instance_index;
            meshlet.meshlet_index = meshlet_index;

            PerInstanceDataOutput[data_output_index] = meshlet;
        }
    }
}

// TODO - We can actually remove this entirely since we can just rebind the UAV as this pass does zero processing. Must ensure values set to 1 though on upload.
[numthreads(1, 1, 1)]
void build_indirect_arguments()
{
    // Align to 32 for indirect dispatch size...
    IndirectArgumentBuffer[0] = AtomicCounter[0];
    IndirectArgumentBuffer[1] = 1;
    IndirectArgumentBuffer[2] = 2;
}

struct PSInput
{
    float4 position : SV_POSITION;
    float3 colour : COLOR0;
    float2 uvs : TEXCOORD0;
    float3 normals : NORMAL;  
    uint meshlet_index : COLOR1;   
    noperspective float3 world_position : COLOR2;
};

// SV_GroupThreadID 0 - 128
// SV_GroupID 0 - dispatchx
// SV_GroupThreadID 0 - 128 * dispatchx

[numthreads(128, 1, 1)]
[OutputTopology("triangle")]
void ms_main(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID, out indices uint3 tris[124], out vertices PSInput verts[64])
{   
    bool valid_thread = gid.x < AtomicCounter[0];
    uint out_vertex_count = 0;
    uint out_triangle_count = 0;
    
    PerInstanceProcessingData_Output instance_stage_output;
    MeshletData meshlet;
    InstanceData instance;
    //ModelData model;
    
    if (valid_thread)
    {
        instance_stage_output = PerInstanceDataOutput[gid.x];
        meshlet = Meshlets[instance_stage_output.meshlet_index];
        instance = Instances[instance_stage_output.instance_index];
        // model = Models[instance.model_index];
        
        out_vertex_count = meshlet.vertex_count;
        out_triangle_count = meshlet.triangle_count;
    }

    SetMeshOutputCounts(out_vertex_count, out_triangle_count);

    if (valid_thread)
    {              
        if (gtid.x < meshlet.vertex_count)
        {
            uint vertex_index = UniqueVertexIndices[meshlet.vertex_offset + gtid.x];
            Vertex v = Vertices[vertex_index];
            float4 outpos;
            float3 world_outpos;
            world_outpos = outpos = mul(float4(v.position, 1.0f), instance.model_matrix);
            outpos = mul(outpos, camera.view_projection_matrix);
                
            float3 outnormals;
            outnormals = normalize(mul(v.normals, (float3x3) instance.model_matrix));
            
            PSInput vertex_output;
            vertex_output.position = outpos;
            vertex_output.world_position = world_outpos;
            vertex_output.colour = v.colour;
            vertex_output.uvs = v.uvs;
            vertex_output.normals = outnormals;
            vertex_output.meshlet_index = instance_stage_output.meshlet_index;
            verts[gtid.x] = vertex_output;
        }
        
        if (gtid.x < meshlet.triangle_count)
        {
            uint offset = meshlet.triangle_offset + gtid.x;
            uint packedIndices = PrimitiveIndices[offset];
            tris[gtid.x] = uint3(packedIndices & 0xFF, (packedIndices >> 8) & 0xFF, (packedIndices >> 16) & 0xFF);
        }
    }
}

float4 ps_main(PSInput input) : SV_TARGET
{
    return 0.0.xxxx;
}