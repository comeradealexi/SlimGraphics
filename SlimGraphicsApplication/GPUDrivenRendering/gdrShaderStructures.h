#ifndef GDR_HEADER_SHARED_STRUCTURE
#define GDR_HEADER_SHARED_STRUCTURE

#ifdef __cplusplus
namespace gdr {
#endif

#include "../ShaderShaderStructIncludeBegin.h"

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

	struct PerInstanceProcessingData
	{
		uint meshlets_to_process_count; // meshlet count * instance count
		uint meshlets_per_instance;
		uint meshlet_offset;
		uint instance_offset;
	};

	struct PerInstanceProcessingData_Output
	{
		uint instance_index;
		uint meshlet_index;
	};

	struct InstanceData
	{
		row_major float4x4 model_matrix;
		float4 colour;

		uint model_index;
	};

	struct ModelData
	{
		uint meshlet_offset;
		uint meshlet_count;
		uint meshlet_vertex_buffer_offset;
		uint meshlet_pad1;

		// Sphere
		float4 sphere_centre;
		float4 sphere_size;		// x = sphere radius - TODO - Doesn't need to be float4

		// AABB
		float4 aabb_center;  // Center of the box.
		float4 aabb_extents; // Distance from the center to each side.

		// OBB
		float4 obb_center;       // Center of the box.
		float4 obb_extents;      // Distance from the center to each side.
		float4 obb_orientation;  // Unit quaternion representing rotation (box -> world).
	};

	struct MaterialData
	{
		uint texture_diffuse_index;
		uint texture_specular_index;
		uint texture_normals_index;
		uint texture_opacity_index;
	};

	struct MeshletData
	{
		/* offsets within meshlet_vertices and meshlet_triangles arrays with meshlet data */
		uint vertex_buffer_offset;
		uint vertex_offset;
		uint triangle_offset;

		/* number of vertices and triangles used in the meshlet; data is stored in consecutive range defined by offset and count */
		uint vertex_count;
		uint triangle_count;

		// Meshlet Culling Data
		float4 spherepos_xyz_radius_w;
		float4 cone_apex_xyz;
		float4 cone_axis_xyz_cone_cutoff_w;

		int cone_axis_s8xyz_cone_cutoff_s8w;
		int pad1;
		int pad2;
		int pad3;

		// AABB
		float4 aabb_center;  // Center of the box.
		float4 aabb_extents; // Distance from the center to each side.

		// OBB
		float4 obb_center;       // Center of the box.
		float4 obb_extents;      // Distance from the center to each side.
		float4 obb_orientation;  // Unit quaternion representing rotation (box -> world).
	};

#include "../ShaderShaderStructIncludeEnd.h"

#ifdef __cplusplus
	/*namespace gdr*/ }
#endif

#endif