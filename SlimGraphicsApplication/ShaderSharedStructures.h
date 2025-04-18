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
#else

#endif

struct ModelData
{
	row_major float4x4 model_matrix;

	#define SHADING_MODE_DEFAULT  0 
	#define SHADING_MODE_PRIMITIVEORDER  1 
	#define SHADING_MODE_VERTEXORDER  2 
	#define SHADING_MODE_PIXELORDER  3 
	int shading_mode;
	int unused1;
	int unused2;
	float vertex_shading_mod;

	uint vertex_count; // Total vertices in current draw call
	uint primitive_count; // Total primitives in current draw all
	float time_total; // Continuous increasing time in seconds
	float time_frame_delta; // Frame time delta in seconds

	float4 pixel_order_data1; //x=multiplier, y=mod, z=use_colours, w=shade_over_time	
	float4 pixel_order_data2;  //x=pixel_to_shade, y=colour_range
};

struct CameraData
{
	row_major float4x4 view_matrix;
	row_major float4x4 projection_matrix;
	row_major float4x4 view_projection_matrix;
	float4 screen_dimensions_and_depth_info;
	float4 camera_position;
	float4 camera_direction;
};

#ifdef __cplusplus
} // namespace ShaderStructs
#undef row_major
#undef bool
#endif

#endif