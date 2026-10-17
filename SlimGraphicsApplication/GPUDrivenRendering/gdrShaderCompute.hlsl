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
StructuredBuffer<PerInstanceProcessingData_Input> PerInstanceDataInput : register(t6);

// Write Buffers
RWStructuredBuffer<PerInstanceProcessingData_Output> PerInstanceDataOutput : register(u0);
RWStructuredBuffer<uint> IndirectArgumentBuffer : register(u1);

cbuffer PerInstanceProcessingDataCBuffer : register(b0)
{
    uint instance_count;
    uint pad1;
    uint pad2;
    uint pad3;
};

[numthreads(32, 1, 1)]
void per_instance_processing_main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint thread_id = dispatch_id.x;
    
    if (thread_id < instance_count)
    {
        PerInstanceProcessingData_Input pipd = PerInstanceDataInput[thread_id];
        InstanceData instance = Instances[pipd.instance_index];
        ModelData model = Models[pipd.model_index];
        
        uint data_output_index; // TODO: This assumes all meshlets are visible
        InterlockedAdd(IndirectArgumentBuffer[0], model.meshlet_count, data_output_index);
       
        // TODO: We're doing far too much processing per-thread below and it's going to diverge heavily between threads
        for (uint meshlet_idx = 0; meshlet_idx < model.meshlet_count; meshlet_idx++)
        {
            // TODO: Visibility and culling checks before adding to list
            
            PerInstanceProcessingData_Output meshlet;
            meshlet.instance_index = pipd.instance_index;
            meshlet.meshlet_index = meshlet_idx;

            PerInstanceDataOutput[data_output_index + meshlet_idx] = meshlet;
        }
    }
}