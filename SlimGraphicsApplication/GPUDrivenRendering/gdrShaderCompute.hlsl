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

cbuffer PerInstanceProcessingDataCBuffer : register(b0)
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

[numthreads(1, 1, 1)]
void build_indirect_arguments()
{
    // Align to 32 for indirect dispatch size...
    IndirectArgumentBuffer[0] = (AtomicCounter[0] + 32 - 1) / 32 * 32;
}

struct PSInput
{
    float4 position : SV_POSITION;
    float3 colour : COLOR0;
    float2 uvs : TEXCOORD0;
    float3 normals : NORMAL;  
    uint MeshletIndex : COLOR1;   
    noperspective float3 world_position : COLOR2;
};

[NumThreads(32, 1, 1)]
[OutputTopology("triangle")]
void ms_main(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID, out indices uint3 tris[124], out vertices PSInput verts[64])
{
    if (dtid.x < AtomicCounter[0])
    {
        PerInstanceProcessingData_Output instance_stage_output = PerInstanceDataOutput[dtid.x];
        MeshletData meshlet = Meshlets[instance_stage_output.meshlet_index];
        InstanceData instance = Instances[instance_stage_output.instance_index];
        //ModelData model = Models[instance.model_index];
        
    }
}

float4 ps_main(PSInput input) : SV_TARGET
{
    return 0.0.xxxx;
}