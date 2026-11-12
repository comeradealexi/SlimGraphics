#include "gdrShaderStructures.h"

// HybridBitonicSort.hlsl
// Two entry points:
//  - LocalTileSortCS : sorts one tile (TILE_SIZE) per group using groupshared memory
//  - GlobalMergeCS   : performs one global bitonic merge pass for given (k, j)
// Data type: KeyValue (uint key + uint value). Modify comparator for float keys.

static const float FLT_MAX = 3.402823466e+38f;
static const float FLT_MIN = 1.175494351e-38;
#if GDR_SORT_DESCENDING
#define GDR_INVALID_KEY FLT_MIN
#else
#define GDR_INVALID_KEY FLT_MAX
#endif

#ifndef TILE_SIZE
#define TILE_SIZE 32
#endif

#ifndef THREADS_PER_GROUP
#define THREADS_PER_GROUP TILE_SIZE
#endif

RWStructuredBuffer<PerInstanceProcessingData_Output> InputBuffer : register(u0);
RWStructuredBuffer<uint4> AtomicCounter : register(u1);

// Local tile params (can be same buffer, but we'll read only necessary fields)
cbuffer GlobalMergeParams : register(b0)
{
    uint Iter_K; // subsequence size for this pass (2, 4, 8, ... up to N)
    uint Iter_J; // half-subsequence (k >> 1)
    uint pad3;
    uint pad4;
}

// ---------- groupshared memory ----------
groupshared PerInstanceProcessingData_Output s_data[TILE_SIZE];

// Compare two KeyValue and return true if A > B (for swap decision in ascending sort)
bool KeyGreaterKV(in PerInstanceProcessingData_Output A, in PerInstanceProcessingData_Output B)
{
    #if GDR_SORT_DESCENDING
    return A.sort_key < B.sort_key;
    #else
    return A.sort_key > B.sort_key;
    #endif
}

// ---------- Local tile sort: sorts TILE_SIZE elements per group ----------
// Sorts elements 0->31, 32->63, 64->95, etc...
[numthreads(THREADS_PER_GROUP, 1, 1)]
void LocalTileSortCS(uint3 DTid : SV_DispatchThreadID,
                     uint3 GTid : SV_GroupThreadID,
                     uint3 GId : SV_GroupID)
{
    // group_index: The unique index of the current thread (or data element) in the parallel execution block.
    uint group_index = GTid.x; // within [0, TILE_SIZE)
    
    uint thread_index = GId.x;
    uint baseIdx = thread_index * TILE_SIZE;
    uint idx = baseIdx + group_index;
    
    // Half of threads are ascending and the others descending
    // Even = Ascending | Odd = Descending
    bool ascending = (thread_index % 2) == 0;

    // Load into shared memory (guard out-of-range)
    PerInstanceProcessingData_Output key_value;
    if (idx < AtomicCounter[0].x)
    {
        key_value = InputBuffer[idx];
    }
    else
    {
        // For padded regions, set sentinel keys so they go to the end
        key_value.sort_key = GDR_INVALID_KEY;
        key_value.instance_index = 0xFFFFFFFFu;
        key_value.meshlet_index = 0xFFFFFFFFu;
        key_value.pad = 0xFFFFFFFFu;

    }
    s_data[group_index] = key_value;
    GroupMemoryBarrierWithGroupSync();

    // In-group iterative bitonic network
    // We run full bitonic network up to TILE_SIZE so after this tile is fully sorted.
    /*
        2 -> (1)
        4 -> (2 -> 1)
        8 -> (4 -> 2 -> 1)
        16 -> (8 -> 4 -> 2 -> 1)
        32 -> (16 -> 8-> 4 -> 2 -> 1)
    */
    for (uint k = 2u; k <= TILE_SIZE; k <<= 1u)
    {
        // Swap our ascending/descending for 50% of threads
        bool subseq_ascending = ((group_index & k) == 0u) ? (ascending != 0u) : (ascending == 0u);
        
        for (uint j = k >> 1u; j > 0u; j >>= 1u)
        {
            
            /* 
                ixj is the element we are comparing against.
                We XOR group_index with j.
                XORing here will always force a specific bit in group_index to be flipped.
                
                Example:
                group_index 0000 and j is 0001: ixj -> 1
                group_index 0001 and j is 0001: ixj -> 0
            
                This guarantees that each thread is comparing a unique value each iteration
            */
            uint ixj = group_index ^ j;
            
            // We only want to make one comparison e.g. comparing array element 0 with 1 - no point also comparing element 1 with 0.
            if (ixj > group_index) // TODO: 50% of threads don't enter this so is not using 100% of GPU resources
            {
                PerInstanceProcessingData_Output A = s_data[group_index];
                PerInstanceProcessingData_Output B = s_data[ixj];

                if (KeyGreaterKV(A, B) == subseq_ascending)
                {
                    // swap values
                    s_data[group_index] = B;
                    s_data[ixj] = A;
                }
            }
            
            // Ensure every thread has completed before continuing
            GroupMemoryBarrierWithGroupSync();
        }
    }
    
    // After this point, our TILE_SIZE sections should be sorted.

    // Write back
    //if (idx < AtomicCounter[0])
    {
        InputBuffer[idx] = s_data[group_index];
    }
}

[numthreads(TILE_SIZE, 1, 1)]
void GlobalMergeCS(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID /* 0-31 */, uint3 GId : SV_GroupID /* 0 - (DISPATCH_X - 1) */)
{
    uint outer_index = DTid.x / (Iter_K / 2);
    uint outer_offset = outer_index * (Iter_K);
    bool ascending = (outer_index % 2) == 0; // ascending then descending
    uint inner_dispatch_id = DTid.x % (Iter_K / 2); // 0 to K - 1

    uint local_index = (uint) inner_dispatch_id % (uint) Iter_J; // 0 - (J - 1)
    uint inner_offset = (inner_dispatch_id / Iter_J) * Iter_J * 2;

    {
        uint index_a = local_index;
        uint index_b = index_a ^ Iter_J;
        
        index_a += inner_offset + outer_offset;
        index_b += inner_offset + outer_offset;     
        
        PerInstanceProcessingData_Output A = InputBuffer[index_a];
        PerInstanceProcessingData_Output B = InputBuffer[index_b];
        
        #if GDR_SORT_DESCENDING
        ascending = !ascending;
        #endif
        
        bool swap = ascending ? (A.sort_key > B.sort_key) : (A.sort_key < B.sort_key);
        if (swap)
        {
            InputBuffer[index_a] = B;
            InputBuffer[index_b] = A;
        }
    }
}
