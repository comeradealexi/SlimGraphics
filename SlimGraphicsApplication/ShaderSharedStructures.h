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

	bool shade_vertex_order;
	bool shade_primitive_order;
	bool unused1;
	bool unused2;

	uint vertex_count; // Total vertices in current draw call
	uint primitive_count; // Total primitives in current draw all
	float time_total; // Continuous increasing time in seconds
	float time_frame_delta; // Frame time delta in seconds
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