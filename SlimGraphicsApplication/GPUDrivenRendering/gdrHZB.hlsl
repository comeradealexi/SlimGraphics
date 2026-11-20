// Hierarchical Z-Buffer build.
//
// Each 32x32 thread group reduces a 32x32 tile of its source level into up to 5 further mips (16x16 .. 1x1).
//  - FIRST_SAMPLE_FROM_DEPTH_SRV: source is the depth SRV; it is copied into HZBTexture_mip0 and mips 1-5 are produced.
//  - Otherwise: source is HZBTexture_mip0 (bound to an existing HZB level) and up to 5 coarser levels are produced.
//
// The HZB stores the FARTHEST depth of each footprint (max, for a standard 0 = near / 1 = far depth buffer) so that
// occlusion tests against it are conservative. The HZB texture is sized to a power of two (see gdrHZB.cpp) so every
// level halves exactly; reads outside the source are clamped to the edge, which never makes a texel nearer than the
// real depth it covers.
#define TILE_SIZE 32
Texture2D<float> DepthTexture : register(t0);
RWTexture2D<float> HZBTexture_mip0 : register(u0);
RWTexture2D<float> HZBTexture_mip1 : register(u1);
RWTexture2D<float> HZBTexture_mip2 : register(u2);
RWTexture2D<float> HZBTexture_mip3 : register(u3);
RWTexture2D<float> HZBTexture_mip4 : register(u4);
RWTexture2D<float> HZBTexture_mip5 : register(u5);

groupshared float gs_data[TILE_SIZE][TILE_SIZE];

cbuffer HZBConstantData : register(b0)
{
    uint output_mip_count; // Number of levels (1-5) to write after the source level (u1..u5)
    uint pad0;
    uint pad1;
    uint pad2;
};

float ReduceDepth(float a, float b, float c, float d)
{
    return max(max(a, b), max(c, d));
}

// Reduces the (tile_size x tile_size) block at the top-left of gs_data into (tile_size/2)^2 values,
// writing the result to the top-left of gs_data and to 'output_value' for the writing thread.
// 'step' is the distance between source samples in thread-id space (1, 2, 4, 8, 16).
bool ReduceLevel(uint2 gtid, uint step, out float output_value)
{
    output_value = 0.0f;
    const uint mask = (step * 2) - 1;
    const bool writer = (gtid.x & mask) == 0 && (gtid.y & mask) == 0;
    const uint2 src = gtid / step; // Source data is compacted at the top-left of gs_data
    if (writer)
    {
        output_value = ReduceDepth(gs_data[src.y][src.x], gs_data[src.y][src.x + 1], gs_data[src.y + 1][src.x], gs_data[src.y + 1][src.x + 1]);
    }
    GroupMemoryBarrierWithGroupSync();
    if (writer)
    {
        const uint2 dst = gtid / (step * 2);
        gs_data[dst.y][dst.x] = output_value;
    }
    GroupMemoryBarrierWithGroupSync();
    return writer;
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void build_hzb(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 GId : SV_GroupID)
{
    uint src_width, src_height;
#ifdef FIRST_SAMPLE_FROM_DEPTH_SRV
    DepthTexture.GetDimensions(src_width, src_height);
#else
    HZBTexture_mip0.GetDimensions(src_width, src_height);
#endif
    // Clamp to the edge so threads outside the source never contribute a 'near' value
    const uint2 src_coord = min(DTid.xy, uint2(src_width - 1, src_height - 1));

#ifdef FIRST_SAMPLE_FROM_DEPTH_SRV
    const float depth = DepthTexture.Load(int3(src_coord, 0));
    HZBTexture_mip0[DTid.xy] = depth; // Out-of-bounds UAV writes are discarded
#else
    const float depth = HZBTexture_mip0[src_coord];
#endif
    // Unused slots (when fewer than 5 levels remain) are bound to an existing level and must not be written.
    const uint mips_to_write = min(output_mip_count, 5u);
    gs_data[GTid.y][GTid.x] = depth;
    GroupMemoryBarrierWithGroupSync();

    float value;
    // All threads take part in every reduction (they contain group barriers); writes are skipped past mips_to_write.
    if (ReduceLevel(GTid.xy, 1, value) && mips_to_write > 0) HZBTexture_mip1[DTid.xy / 2] = value;
    if (ReduceLevel(GTid.xy, 2, value) && mips_to_write > 1) HZBTexture_mip2[DTid.xy / 4] = value;
    if (ReduceLevel(GTid.xy, 4, value) && mips_to_write > 2) HZBTexture_mip3[DTid.xy / 8] = value;
    if (ReduceLevel(GTid.xy, 8, value) && mips_to_write > 3) HZBTexture_mip4[DTid.xy / 16] = value;
    if (ReduceLevel(GTid.xy, 16, value) && mips_to_write > 4) HZBTexture_mip5[DTid.xy / 32] = value;
}
