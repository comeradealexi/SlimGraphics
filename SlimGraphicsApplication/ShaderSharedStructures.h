#ifndef HEADER_SHARED_STRUCTURE
#define HEADER_SHARED_STRUCTURE

#ifdef __cplusplus
#define row_major 
namespace ShaderStructs 
{
	using float4 = DirectX::XMFLOAT4A;
	using float4x4 = DirectX::XMMATRIX;
#else

#endif

struct ModelData
{
	row_major float4x4 model_matrix;
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
#endif

#endif