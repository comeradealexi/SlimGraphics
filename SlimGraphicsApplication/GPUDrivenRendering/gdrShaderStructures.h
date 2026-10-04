#ifndef GDR_HEADER_SHARED_STRUCTURE
#define GDR_HEADER_SHARED_STRUCTURE

#include "../ShaderShaderStructIncludeBegin.h"

struct ModelData
{	
	float4 obb_center;		// Center of the box.
	float4 obb_extents;		// Distance from the center to each side.
	float4 obb_orientation;	// Unit quaternion representing rotation (box -> world).
	
	float4 aabb_centre;		// Center of the box.
	float4 aabb_extents;	// Distance from the center to each side.

	float4 sphere_size;		// x = sphere radius - TODO - Doesn't need to be float4
};

struct ObjectData
{
	row_major float4x4 model_matrix;
	row_major float4x4 model_matrix_inverse;
	uint4 model_index; // x = model index
	float4 colour; // RGBA
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

#include "../ShaderShaderStructIncludeEnd.h"
#endif