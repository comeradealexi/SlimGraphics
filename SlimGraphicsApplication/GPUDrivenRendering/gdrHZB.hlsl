//#define FIRST_SAMPLE_FROM_DEPTH_SRV
#define TILE_SIZE 32
Texture2D<float> DepthTexture : register(t0);
RWTexture2D<float> HZBTexture_mip0 : register(u0);
RWTexture2D<float> HZBTexture_mip1 : register(u1);
RWTexture2D<float> HZBTexture_mip2 : register(u2);
RWTexture2D<float> HZBTexture_mip3 : register(u3);
RWTexture2D<float> HZBTexture_mip4 : register(u4);
RWTexture2D<float> HZBTexture_mip5 : register(u5);
//RWTexture2D<float> HZBTexture_mip6 : register(u6);

groupshared float gs_data[TILE_SIZE][TILE_SIZE];

cbuffer HZBConstantData : register(b0)
{
    uint mip_levels;
    uint pad0;
    uint pad1;
    uint pad2;
};

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void build_hzb(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 GId : SV_GroupID)
{
    // Load depth values into shared memory
    
    uint2 scaled_gtid = GTid.xy;
    
#ifdef FIRST_SAMPLE_FROM_DEPTH_SRV
    gs_data[GTid.y][GTid.x] = DepthTexture.Load(int3(DTid.xy, 0));

	// Mip 0
    uint2 mip0_coordinates = DTid.xy;
	HZBTexture_mip0[mip0_coordinates] = gs_data[GTid.y][GTid.x];
#else
    gs_data[GTid.y][GTid.x] = HZBTexture_mip0[DTid.xy];
#endif
    GroupMemoryBarrierWithGroupSync();
	
    float max_depth;

	// Mip 1
    if ((DTid.x & 1) == 0 && (DTid.y & 1) == 0)
	{
        max_depth = min(gs_data[GTid.y][GTid.x], gs_data[GTid.y][GTid.x + 1]);
        max_depth = min(max_depth, gs_data[GTid.y + 1][GTid.x]);
		max_depth = min(max_depth, gs_data[GTid.y + 1][GTid.x + 1]);
		
        uint2 mip1_coordinates = DTid.xy / 2;
		HZBTexture_mip1[mip1_coordinates] = max_depth;
    }

	GroupMemoryBarrierWithGroupSync();
	
    if ((DTid.x & 1) == 0 && (DTid.y & 1) == 0)
    {
        gs_data[GTid.y / 2][GTid.x / 2] = max_depth;
    }
	
    GroupMemoryBarrierWithGroupSync();

    scaled_gtid = GTid.xy / 2;
    
	// Mip 2
    if ((DTid.x & 3) == 0 && (DTid.y & 3) == 0)
	{
        max_depth = min(gs_data[scaled_gtid.y][scaled_gtid.x], gs_data[scaled_gtid.y][scaled_gtid.x + 1]);
        max_depth = min(max_depth, gs_data[scaled_gtid.y + 1][scaled_gtid.x]);
        max_depth = min(max_depth, gs_data[scaled_gtid.y + 1][scaled_gtid.x + 1]);
		
        uint2 mip2_coordinates = DTid.xy / 4;
		HZBTexture_mip2[mip2_coordinates] = max_depth;
	}
    
    GroupMemoryBarrierWithGroupSync();
    
    if ((DTid.x & 3) == 0 && (DTid.y & 3) == 0)
    {
        gs_data[GTid.y / 4][GTid.x / 4] = max_depth;
    }

    GroupMemoryBarrierWithGroupSync();

    scaled_gtid = GTid.xy / 4;
    
	// Mip 3
    if ((DTid.x & 7) == 0 && (DTid.y & 7) == 0)
	{
        max_depth = min(gs_data[scaled_gtid.y][scaled_gtid.x], gs_data[scaled_gtid.y][scaled_gtid.x + 1]);
        max_depth = min(max_depth, gs_data[scaled_gtid.y + 1][scaled_gtid.x]);
        max_depth = min(max_depth, gs_data[scaled_gtid.y + 1][scaled_gtid.x + 1]);
        
		uint2 mip3_coordinates = DTid.xy / 8;
		HZBTexture_mip3[mip3_coordinates] = max_depth;
	}
    
    GroupMemoryBarrierWithGroupSync();
    
    if ((DTid.x & 7) == 0 && (DTid.y & 7) == 0)
    {
        gs_data[GTid.y / 8][GTid.x / 8] = max_depth;
    }
    
    GroupMemoryBarrierWithGroupSync();

    scaled_gtid = GTid.xy / 8;
    
	// Mip 4
    if ((DTid.x & 15) == 0 && (DTid.y & 15) == 0)
    {
        max_depth = min(gs_data[scaled_gtid.y][scaled_gtid.x], gs_data[scaled_gtid.y][scaled_gtid.x + 1]);
        max_depth = min(max_depth, gs_data[scaled_gtid.y + 1][scaled_gtid.x]);
        max_depth = min(max_depth, gs_data[scaled_gtid.y + 1][scaled_gtid.x + 1]);
        
        uint2 mip4_coordinates = DTid.xy / 16;
        HZBTexture_mip4[mip4_coordinates] = max_depth;
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    if ((DTid.x & 15) == 0 && (DTid.y & 15) == 0)
    {
        gs_data[GTid.y / 16][GTid.x / 16] = max_depth;
    }
    
    GroupMemoryBarrierWithGroupSync();

    scaled_gtid = GTid.xy / 16;
    
	// Mip 5
    if ((DTid.x & 31) == 0 && (DTid.y & 31) == 0)
    {
        max_depth = min(gs_data[scaled_gtid.y][scaled_gtid.x], gs_data[scaled_gtid.y][scaled_gtid.x + 1]);
        max_depth = min(max_depth, gs_data[scaled_gtid.y + 1][scaled_gtid.x]);
        max_depth = min(max_depth, gs_data[scaled_gtid.y + 1][scaled_gtid.x + 1]);
        
        uint2 mip5_coordinates = DTid.xy / 32;
        HZBTexture_mip5[mip5_coordinates] = max_depth;
    }
}