#ifndef HEADER_SHARED_STRUCTURE
#define HEADER_SHARED_STRUCTURE

#ifdef __cplusplus
#include <stdint.h>
#define row_major 
#define bool uint32_t
namespace ShaderStructs 
{
	using float4 = DirectX::XMFLOAT4A;
	using float4x4 = DirectX::XMMATRIX;
	using uint = uint32_t;
	using int4 = DirectX::XMINT4;
#else

#endif

struct ModelData
{
	row_major float4x4 model_matrix;
	row_major float4x4 model_matrix_inverse;

	#define SHADING_MODE_DEFAULT  0 
	#define SHADING_MODE_PRIMITIVEORDER  1 
	#define SHADING_MODE_VERTEXORDER  2 
	#define SHADING_MODE_PIXELORDER  3 
	#define SHADING_MODE_MESHLETORDER  4 
	#define SHADING_MODE_MESHLET_CULL_ANGLE  5
	#define SHADING_MODE_WAVE_INTRINSICS  6
	#define SHADING_MODE_AMPLIFICATION_ORDER  7

	int shading_mode;
	int meshlet_count;
	int meshlet_vb_offset;
	float vertex_shading_mod;

	uint vertex_count; // Total vertices in current draw call
	uint primitive_count; // Total primitives in current draw all
	float time_total; // Continuous increasing time in seconds
	float time_frame_delta; // Frame time delta in seconds

	float4 pixel_order_data1; //x=multiplier, y=mod, z=use_colours, w=shade_over_time	
	float4 pixel_order_data2;  //x=pixel_to_shade, y=colour_range

	int4 wave_intrinsics; // x=lanesize, y=mode

	int4 meshlet_culling; // x = cone culling, y = sphere frustum culling
};

struct CameraData
{
	row_major float4x4 view_matrix;
	row_major float4x4 projection_matrix;
	row_major float4x4 view_projection_matrix;
	float4 screen_dimensions_and_depth_info;
	float4 camera_position;
	float4 camera_direction;

	// Camera frustum planes
	float4 Planes[6];
};

struct MeshletCullData
{
	float4 spherepos_xyz_radius_w;

	float4 cone_apex_xyz;

	float4 cone_axis_xyz_cone_cutoff_w;

	int cone_axis_s8xyz_cone_cutoff_s8w;
	int pad1;
	int pad2;
	int pad3;
};

struct TerrainData
{

};

struct MagnifyingGlassData
{
	float4 coordinates; // Source Pixel index = x,y, output texture size =  z,w
	float4 scale; // x = scale
};

struct PostProcessData
{
	int4 mode;
	float4 colour_output_enabled;
};

#ifdef __cplusplus
} // namespace ShaderStructs
#undef row_major
#undef bool
#endif

#endif