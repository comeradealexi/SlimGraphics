Texture2D<float> DepthTexture : register(t0);
RWTexture2D<float> HZBTexture_mip0 : register(u0);
RWTexture2D<float> HZBTexture_mip1 : register(u1);
RWTexture2D<float> HZBTexture_mip2 : register(u2);
RWTexture2D<float> HZBTexture_mip3 : register(u3);

groupshared float gs_data[8][8];

[numthreads(8, 8, 1)]
void build_hzb(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 GId : SV_GroupID)
{
	// Load depth values into shared memory
    gs_data[GTid.y][GTid.x] = DepthTexture.Load(int3(DTid.xy, 0));

	// Mip 0
    uint2 mip0_coordinates = DTid.xy;
	HZBTexture_mip0[mip0_coordinates] = gs_data[GTid.y][GTid.x];

    GroupMemoryBarrierWithGroupSync();
	
	// Mip 1
    if ((DTid.x & 1) == 0 && (DTid.y & 1) == 0)
	{
        float max_depth = max(gs_data[GTid.y][GTid.x], gs_data[GTid.y][GTid.x + 1]);
        max_depth = max(max_depth, gs_data[GTid.y + 1][GTid.x]);
		max_depth = max(max_depth, gs_data[GTid.y + 1][GTid.x + 1]);
		
        uint2 mip1_coordinates = DTid.xy / 2;
		HZBTexture_mip1[mip1_coordinates] = max_depth;
        gs_data[GTid.y / 2][GTid.x / 2] = max_depth;
    }

	GroupMemoryBarrierWithGroupSync();

	// Mip 2
    if ((DTid.x & 3) == 0 && (DTid.y & 3) == 0)
	{
        float max_depth = max(gs_data[GTid.y][GTid.x], gs_data[GTid.y][GTid.x + 1]);
        max_depth = max(max_depth, gs_data[GTid.y + 1][GTid.x]);
        max_depth = max(max_depth, gs_data[GTid.y + 1][GTid.x + 1]);
		
        uint2 mip2_coordinates = DTid.xy / 4;
		HZBTexture_mip2[mip2_coordinates] = max_depth;
		gs_data[GTid.y / 4][GTid.x / 4] = max_depth;
	}

	GroupMemoryBarrierWithGroupSync();

	// Mip 3
    if ((DTid.x & 7) == 0 && (DTid.y & 7) == 0)
	{
        float max_depth = max(gs_data[GTid.y][GTid.x], gs_data[GTid.y][GTid.x + 1]);
        max_depth = max(max_depth, gs_data[GTid.y + 1][GTid.x]);
        max_depth = max(max_depth, gs_data[GTid.y + 1][GTid.x + 1]);
        
		uint2 mip3_coordinates = DTid.xy / 8;
		HZBTexture_mip3[mip3_coordinates] = max_depth;
		gs_data[GTid.y / 8][GTid.x / 8] = max_depth;
	}
}