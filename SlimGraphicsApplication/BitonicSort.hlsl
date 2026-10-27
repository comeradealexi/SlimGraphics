// HybridBitonicSort.hlsl
// Two entry points:
//  - LocalTileSortCS : sorts one tile (TILE_SIZE) per group using groupshared memory
//  - GlobalMergeCS   : performs one global bitonic merge pass for given (k, j)
// Data type: KeyValue (uint key + uint value). Modify comparator for float keys.

#ifndef TILE_SIZE
#define TILE_SIZE 32         // compile-time: 512 or 1024 recommended
#endif

#ifndef THREADS_PER_GROUP
#define THREADS_PER_GROUP TILE_SIZE
#endif

// ---------- Data types ----------
struct KeyValue {
    uint key;
    uint value; // payload, optional
};

// Replace KeyValue with float & payload if needed and update comparisons accordingly.
// Structured buffers bound from CPU:
StructuredBuffer<KeyValue> g_Input  : register(t0);
RWStructuredBuffer<KeyValue> g_Output : register(u0);

// ---------- Constants ----------
cbuffer SortParams : register(b0)
{
    uint g_NumElements;   // array length (power-of-two, padded if necessary)
    uint g_TileSize;      // should equal TILE_SIZE at runtime
    uint g_Ascending;     // 1 = ascending, 0 = descending (global direction)
    uint pad0;
    // For GlobalMergeCS, we will set g_k and g_j via separate params (see below)
};

// Local tile params (can be same buffer, but we'll read only necessary fields)
cbuffer GlobalMergeParams : register(b1)
{
    uint g_k;   // subsequence size for this pass (2, 4, 8, ... up to N)
    uint g_j;   // half-subsequence (k >> 1)
    uint g_dummy0;
    uint g_dummy1;
}

// ---------- groupshared memory ----------
groupshared KeyValue s_data[TILE_SIZE];

// ---------- Helper comparator ----------
inline bool KeyLess(uint a, uint b) { return a < b; }
inline bool KeyGreater(uint a, uint b) { return a > b; }

// Compare two KeyValue and return true if A > B (for swap decision in ascending sort)
inline bool KeyGreaterKV(in KeyValue A, in KeyValue B)
{
    // Primary compare on key. If equal, you could compare value for stable ordering
    return A.key > B.key;
}

// ---------- Local tile sort: sorts TILE_SIZE elements per group ----------
// Sorts elements 0->31, 32->63, 64->95, etc...
[numthreads(THREADS_PER_GROUP, 1, 1)]
void LocalTileSortCS(uint3 DTid : SV_DispatchThreadID,
                     uint3 GTid : SV_GroupThreadID,
                     uint3 GId  : SV_GroupID)
{
    // group_index: The unique index of the current thread (or data element) in the parallel execution block.
    uint group_index = GTid.x; // within [0, TILE_SIZE)
    
    uint thread_index = GId.x;
    uint baseIdx  = thread_index * TILE_SIZE;
    uint idx      = baseIdx + group_index;
    
    // Half of threads are ascending and the others descending
    // Even = Ascending | Odd = Descending
    bool ascending = (thread_index % 2) == 0;

    // Load into shared memory (guard out-of-range)
    KeyValue myKV;
    if (idx < g_NumElements) {       
        myKV = g_Input[idx];
    } else {
        // For padded regions, set sentinel keys so they go to the end
        myKV.key = 0xFFFFFFFFu; // largest unsigned value (for ascending)
        myKV.value = 0;
    }
    s_data[group_index] = myKV;
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
                KeyValue A = s_data[group_index];
                KeyValue B = s_data[ixj];

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
    if (idx < g_NumElements) 
    {
        g_Output[idx] = s_data[group_index];
    }
}

// Now when running this step, we have our list sorted in chunks of 32.
// We now need to merge these together until the entire list is sorted.

// K runs from (2*TILE_SIZE) -> array size(always power of 2)
    // J runs from (k / 2) until 1

/* So to give an example on the first iteration
    k = 32
    j = 16
    
    We dispatch enough threads for the entire list. 

    Lets focus on the first thread group 0:

    Thread 0:
    outer_index = 0 / (32/2) = 0
    outer_index = 0 * 32 = 0;
    ascending = true;
    inner_dispatch_id = 0;

    local_index = 0;
    inner_offset = 0;

    index_a = 0;
    index_b = 0 ^ 16 = 17;
*/
[numthreads(TILE_SIZE, 1, 1)]
void GlobalMergeCS(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID /* 0-31 */, uint3 GId : SV_GroupID /* 0 - (DISPATCH_X - 1) */)
{         
    uint outer_index = DTid.x / (g_k / 2);
    uint outer_offset = outer_index * (g_k);
    bool ascending = (outer_index % 2) == 0; // ascending then descending
    uint inner_dispatch_id = DTid.x % (g_k / 2); // 0 to g_k - 1

    uint local_index = (uint)inner_dispatch_id % (uint) g_j; // 0 - (g_j - 1)
    uint inner_offset = (inner_dispatch_id / g_j) * g_j * 2;

    {
        uint index_a = local_index;
        uint index_b = index_a ^ g_j;
        
        index_a += inner_offset + outer_offset;
        index_b += inner_offset + outer_offset;
        
        KeyValue A = g_Output[index_a];
        KeyValue B = g_Output[index_b];
        
        bool swap = ascending ? (A.key > B.key) : (A.key < B.key);
        if (swap)
        {
            g_Output[index_a] = B;
            g_Output[index_b] = A;
        }
    }
}
