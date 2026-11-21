#ifndef PBR_RADIANCE_CACHE_HLSLI
#define PBR_RADIANCE_CACHE_HLSLI

// World space radiance cache: a hash grid of irradiance at surfaces (cells sized with the distance to the camera, split by
// the dominant normal axis). Visible pixels add their one sample per frame irradiance estimates; diffuse and specular ray
// hits read it for the light that reaches them over further bounces. Every frame adds a bounce, so it converges to multiple
// bounce global illumination. Cells that stop receiving samples age out.
//
// frame.radiance_cache: x = cell size at 1 unit from the camera, y = minimum cell size, z = blend, w = capacity

RWStructuredBuffer<uint> CacheKeys : register(u3);          // Checksum of the cell in each slot (0 = empty)
RWStructuredBuffer<uint4> CacheAccumulators : register(u4); // Fixed point irradiance sum (rgb), sample count
RWStructuredBuffer<float4> CacheValues : register(u5);      // Irradiance (rgb), frames since the last sample (w)

static const uint CACHE_PROBES = 8;
static const float CACHE_FIXED_POINT = 4096.0f; // Accumulators hold irradiance * exposure * this (rounded stochastically)

uint CacheHash(uint3 cell, uint level, uint axis, uint seed)
{
    uint h = PCGHash(cell.x + seed);
    h = PCGHash(h ^ cell.y);
    h = PCGHash(h ^ cell.z);
    return PCGHash(h ^ (level | (axis << 8)));
}

// Cell of a surface point: 'level' = log2 of the cell size, 'axis' = dominant normal direction (0..5)
void CacheCell(float3 position, float3 normal, out uint3 cell, out uint level, out uint axis)
{
    float distance_to_camera = length(position - frame.camera_position.xyz);
    float size = max(frame.radiance_cache.y, frame.radiance_cache.x * distance_to_camera);
    float l = ceil(log2(size));
    level = (uint)(l + 64.0f);
    float cell_size = exp2(l);
    cell = (uint3)(int3)floor(position / cell_size);
    float3 a = abs(normal);
    axis = a.x > a.y && a.x > a.z ? (normal.x > 0.0f ? 0u : 1u) : (a.y > a.z ? (normal.y > 0.0f ? 2u : 3u) : (normal.z > 0.0f ? 4u : 5u));
}

bool RadianceCacheLookup(float3 position, float3 normal, float hit_distance, out float3 irradiance)
{
    irradiance = 0.0f;
    uint3 cell;
    uint level, axis;
    CacheCell(position, normal, cell, level, axis);
    uint capacity = (uint)frame.radiance_cache.w;
    uint slot = CacheHash(cell, level, axis, 0x9E3779B9u) % capacity;
    uint checksum = max(CacheHash(cell, level, axis, 0x85EBCA6Bu), 1u);
    for (uint i = 0; i < CACHE_PROBES; i++)
    {
        uint s = (slot + i) % capacity;
        uint key = CacheKeys[s];
        if (key == checksum)
        {
            float4 value = CacheValues[s];
            irradiance = value.rgb;
            return value.w < 1000.0f; // w >= 1000: inserted, no value yet
        }
        if (key == 0)
        {
            return false;
        }
    }
    return false;
}

// A subset of pixels updates the cache each frame: a cell covers many pixels, and it keeps rays and atomics down
// One pixel in 'interval' (rotating each frame) feeds the cache (pbr.cache.update_interval)
bool RadianceCacheUpdatePixel(uint2 pixel, uint frame_index, uint interval)
{
    return ((pixel.x + pixel.y * 3 + frame_index) % max(interval, 1u)) == 0;
}

void RadianceCacheAccumulate(float3 position, float3 normal, float distance_to_camera, float3 irradiance, uint2 pixel, uint frame_index, uint interval)
{
    if (!RadianceCacheUpdatePixel(pixel, frame_index, interval))
    {
        return;
    }
    uint3 cell;
    uint level, axis;
    CacheCell(position, normal, cell, level, axis);
    uint capacity = (uint)frame.radiance_cache.w;
    uint slot = CacheHash(cell, level, axis, 0x9E3779B9u) % capacity;
    uint checksum = max(CacheHash(cell, level, axis, 0x85EBCA6Bu), 1u);
    float scale = frame.exposure * CACHE_FIXED_POINT;
    // Stochastic rounding keeps the fixed point sums unbiased for dim cells (truncation would darken them)
    uint h = PCGHash(pixel.x + PCGHash(pixel.y + PCGHash(frame_index ^ 0x5bd1e995u)));
    float3 dither = float3(h & 0x3FF, (h >> 10) & 0x3FF, (h >> 20) & 0x3FF) / 1024.0f;
    uint3 fixed_point = (uint3)min(max(irradiance, 0.0f) * scale + dither, 1048576.0f);
    for (uint i = 0; i < CACHE_PROBES; i++)
    {
        uint s = (slot + i) % capacity;
        uint previous;
        InterlockedCompareExchange(CacheKeys[s], 0, checksum, previous);
        if (previous == 0 || previous == checksum)
        {
            InterlockedAdd(CacheAccumulators[s].x, fixed_point.x);
            InterlockedAdd(CacheAccumulators[s].y, fixed_point.y);
            InterlockedAdd(CacheAccumulators[s].z, fixed_point.z);
            InterlockedAdd(CacheAccumulators[s].w, 1);
            return;
        }
    }
}

#endif
