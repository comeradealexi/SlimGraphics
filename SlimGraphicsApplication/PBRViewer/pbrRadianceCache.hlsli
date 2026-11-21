#ifndef PBR_RADIANCE_CACHE_HLSLI
#define PBR_RADIANCE_CACHE_HLSLI

// World space radiance cache: a hash grid of irradiance at surfaces (cells sized with the distance to the camera, split by
// the dominant normal axis). Pixels add their one sample per frame irradiance estimates; diffuse and specular ray hits read
// it for the light that reaches them over further bounces. Every frame adds a bounce, so it converges to multiple bounce
// global illumination. Cells that stop receiving samples age out.

RWStructuredBuffer<uint> CacheKeys : register(u3);          // Checksum of the cell in each slot (0 = empty)
RWStructuredBuffer<uint4> CacheAccumulators : register(u4); // Fixed point irradiance sum (rgb), sample count
RWStructuredBuffer<float4> CacheValues : register(u5);      // Irradiance (rgb), frames since the last sample (w)

static const uint CACHE_PROBES = 8;
static const float CACHE_FIXED_POINT = 4096.0f;     // Accumulators hold irradiance * exposure * this (rounded stochastically)
static const float CACHE_NO_VALUE = 1000.0f;        // CacheValues.w of a cell inserted this frame, without a value yet

// Where a surface point's cell lives: the first slot of its linear probe sequence and the checksum identifying it
struct CacheCell
{
    uint slot;
    uint checksum;
};

CacheCell FindCacheCell(float3 position, float3 normal)
{
    // Cell size: a power of two that grows with the distance from the camera; one cell per dominant normal direction
    float size = max(frame.cache_min_cell_size, frame.cache_cell_size * length(position - frame.camera_position.xyz));
    float level = ceil(log2(size));
    int3 cell = (int3)floor(position / exp2(level));
    float3 a = abs(normal);
    uint axis = a.x > a.y && a.x > a.z ? (normal.x > 0.0f ? 0u : 1u) : (a.y > a.z ? (normal.y > 0.0f ? 2u : 3u) : (normal.z > 0.0f ? 4u : 5u));
    uint h = PCGHash(PCGHash(PCGHash((uint)cell.x) ^ (uint)cell.y) ^ (uint)cell.z);
    h ^= (uint)(level + 64.0f) | (axis << 8);
    CacheCell c;
    c.slot = PCGHash(h ^ 0x9E3779B9u) % frame.cache_capacity;
    c.checksum = max(PCGHash(h ^ 0x85EBCA6Bu), 1u);
    return c;
}

bool RadianceCacheLookup(float3 position, float3 normal, out float3 irradiance)
{
    irradiance = 0.0f;
    CacheCell cell = FindCacheCell(position, normal);
    for (uint i = 0; i < CACHE_PROBES; i++)
    {
        uint s = (cell.slot + i) % frame.cache_capacity;
        uint key = CacheKeys[s];
        if (key == cell.checksum)
        {
            float4 value = CacheValues[s];
            irradiance = value.rgb;
            return value.w < CACHE_NO_VALUE;
        }
        if (key == 0)
        {
            return false;
        }
    }
    return false;
}

// One pixel in 'interval' (rotating each frame) feeds the cache: a cell covers many pixels, and it keeps rays and atomics down
bool RadianceCacheUpdatePixel(uint2 pixel, uint interval)
{
    return ((pixel.x + pixel.y * 3 + frame.frame_index) % max(interval, 1u)) == 0;
}

void RadianceCacheAccumulate(float3 position, float3 normal, float3 irradiance, uint2 pixel)
{
    CacheCell cell = FindCacheCell(position, normal);
    // Stochastic rounding keeps the fixed point sums unbiased for dim cells (truncation would darken them)
    uint h = PixelHash(pixel, frame.frame_index ^ 0x5bd1e995u);
    float3 dither = float3(h & 0x3FF, (h >> 10) & 0x3FF, (h >> 20) & 0x3FF) / 1024.0f;
    uint3 fixed_point = (uint3)min(max(irradiance, 0.0f) * frame.exposure * CACHE_FIXED_POINT + dither, 1048576.0f);
    for (uint i = 0; i < CACHE_PROBES; i++)
    {
        uint s = (cell.slot + i) % frame.cache_capacity;
        uint previous;
        InterlockedCompareExchange(CacheKeys[s], 0, cell.checksum, previous);
        if (previous == 0 || previous == cell.checksum)
        {
            InterlockedAdd(CacheAccumulators[s].x, fixed_point.x);
            InterlockedAdd(CacheAccumulators[s].y, fixed_point.y);
            InterlockedAdd(CacheAccumulators[s].z, fixed_point.z);
            InterlockedAdd(CacheAccumulators[s].w, 1);
            return;
        }
    }
}

// Blends each cell's samples of this frame into its value; cells without samples for a while are evicted
[numthreads(64, 1, 1)]
void radiance_cache_resolve(uint3 dtid : SV_DispatchThreadID)
{
    uint i = dtid.x;
    if (i >= frame.cache_capacity || CacheKeys[i] == 0)
    {
        return;
    }
    uint4 accumulated = CacheAccumulators[i];
    float4 value = CacheValues[i];
    if (accumulated.w > 0)
    {
        float3 irradiance = (float3)accumulated.rgb / (accumulated.w * frame.exposure * CACHE_FIXED_POINT);
        value = float4(value.w >= CACHE_NO_VALUE ? irradiance : lerp(value.rgb, irradiance, frame.cache_blend), 0.0f);
        CacheAccumulators[i] = 0;
    }
    else if (++value.w > 120.0f && value.w < CACHE_NO_VALUE)
    {
        CacheKeys[i] = 0;
        value = float4(0.0f, 0.0f, 0.0f, CACHE_NO_VALUE);
    }
    CacheValues[i] = value;
}

#endif
