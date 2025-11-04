// HybridBitonicSort.hlsl
// Two entry points:
//  - LocalTileSortCS : sorts one tile (TILE_SIZE) per group using groupshared memory
//  - GlobalMergeCS   : performs one global bitonic merge pass for given (k, j)
// Data type: KeyValue (uint key + uint value). Modify comparator for float keys.

#ifndef TILE_SIZE
#define TILE_SIZE 1024         // compile-time: 512 or 1024 recommended
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
[numthreads(THREADS_PER_GROUP, 1, 1)]
void LocalTileSortCS(uint3 DTid : SV_DispatchThreadID,
                     uint3 GTid : SV_GroupThreadID,
                     uint3 GId  : SV_GroupID)
{
    uint localIdx = GTid.x;                 // within [0, TILE_SIZE)
    uint groupIdx = GId.x;
    uint baseIdx  = groupIdx * g_TileSize;
    uint idx      = baseIdx + localIdx;

    // Load into shared memory (guard out-of-range)
    KeyValue myKV;
    if (idx < g_NumElements) {
        myKV = g_Input[idx];
    } else {
        // For padded regions, set sentinel keys so they go to the end
        myKV.key = 0xFFFFFFFFu; // largest unsigned value (for ascending)
        myKV.value = 0;
    }
    s_data[localIdx] = myKV;
    GroupMemoryBarrierWithGroupSync();

    // In-group iterative bitonic network
    // We run full bitonic network up to TILE_SIZE so after this tile is fully sorted.
    for (uint k = 2u; k <= g_TileSize; k <<= 1u)
    {
        for (uint j = k >> 1u; j > 0u; j >>= 1u)
        {
            uint ixj = localIdx ^ j;
            if (ixj > localIdx)
            {
                KeyValue A = s_data[localIdx];
                KeyValue B = s_data[ixj];

                bool subseqAscending = ((localIdx & k) == 0u) ? (g_Ascending != 0u) : (g_Ascending == 0u);
                bool shouldSwap = (KeyGreaterKV(A, B) == subseqAscending);

                if (shouldSwap)
                {
                    // swap values
                    s_data[localIdx] = B;
                    s_data[ixj] = A;
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }

    // Write back
    if (idx < g_NumElements) {
        g_Output[idx] = s_data[localIdx];
    }
}

// ---------- Global merge pass: handles only j >= TILE_SIZE ----------
[numthreads(1024, 1, 1)] // you can change to an appropriate group size (e.g., 256, 512)
void GlobalMergeCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= g_NumElements) return;

    uint ixj = i ^ g_j;
    if (ixj > i)
    {
        KeyValue A = g_Input[i];
        KeyValue B = g_Input[ixj];

        bool ascending = ((i & g_k) == 0u) ? (g_Ascending != 0u) : (g_Ascending == 0u);
        bool swap = (KeyGreaterKV(A, B) == ascending);
        if (swap)
        {
            g_Output[i] = B;
            g_Output[ixj] = A;
        }
        else
        {
            g_Output[i] = A;
            g_Output[ixj] = B;
        }
    }
    else
    {
        // The partner wrote this slot; but for safety copy if partner wasn't executed
        g_Output[i] = g_Input[i];
    }
}
