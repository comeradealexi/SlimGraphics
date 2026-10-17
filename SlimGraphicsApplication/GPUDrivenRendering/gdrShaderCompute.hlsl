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
StructuredBuffer<ModelData> Models : register(t5);

// Write Buffers
RWStructuredBuffer<PerInstanceProcessingData_Output> PerInstanceDataOutput : register(u0);
RWStructuredBuffer<uint> IndirectArgumentBuffer : register(u1);

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
            InterlockedAdd(IndirectArgumentBuffer[0], 1, data_output_index);
            
            PerInstanceProcessingData_Output meshlet;
            meshlet.instance_index = instance_index;
            meshlet.meshlet_index = meshlet_index;

            PerInstanceDataOutput[data_output_index] = meshlet;
        }
    }
}