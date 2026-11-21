#ifndef GDR_HEADER_SHARED_STRUCTURE
#define GDR_HEADER_SHARED_STRUCTURE

#ifdef __cplusplus
namespace gdr {
#endif

#include "../ShaderShaderStructIncludeBegin.h"

	enum class DebugRenderMode : uint
	{
		None,
		SortOrder,	// White = first, black = last
		Albedo,		// Surface colour (textures, material and instance colour) without lighting
		Normals,	// World space shading normal (after normal / bump mapping)
		Lighting,	// Lighting on a white surface
	};

	// MeshletData::meshlet_type
	#define GDR_MESHLET_TYPE_MESH 0u		// Vertices and triangles come from the model buffers
	#define GDR_MESHLET_TYPE_TERRAIN 1u		// A terrain tile: the mesh shader builds a grid from the terrain's height buffer

	// Terrain tiles are GDR_TERRAIN_TILE_QUADS x GDR_TERRAIN_TILE_QUADS quads: (7 + 1)^2 = 64 vertices, 7 * 7 * 2 = 98 triangles,
	// within the mesh shader's 64 vertex / 124 triangle meshlet limits
	#define GDR_TERRAIN_TILE_QUADS 7u
	#define GDR_TERRAIN_TILE_VERTICES ((GDR_TERRAIN_TILE_QUADS + 1u) * (GDR_TERRAIN_TILE_QUADS + 1u))
	#define GDR_TERRAIN_TILE_TRIANGLES (GDR_TERRAIN_TILE_QUADS * GDR_TERRAIN_TILE_QUADS * 2u)

	// MaterialData texture slices that are not used
	#define GDR_NO_TEXTURE 0xFFFFFFFFu

	// MaterialData::flags
	#define GDR_MATERIAL_ALPHA_TEST 1u		// Discard pixels whose opacity (opacity texture, else diffuse alpha) is below 0.5
	#define GDR_MATERIAL_BUMP_MAP 2u		// texture_normals_index is a height (bump) map rather than a tangent space normal map
	#define GDR_MATERIAL_TERRAIN 4u			// Procedural terrain shading (TerrainData of the meshlet's terrain)
	#define GDR_MATERIAL_DOUBLE_SIDED 8u	// Rendered without back face culling, lit from the side facing the viewer

	// Render buckets: meshlets that need different pipeline state (and specialised shaders, so no per pixel branching on
	// meshlet type) are culled into separate regions of the visible meshlet list, each drawn by its own indirect DispatchMesh.
	// Every mesh part belongs to exactly one bucket (gdr::bucket_of_material).
	#define GDR_BUCKET_OPAQUE 0u			// Back face culled, no alpha test: keeps early depth rejection
	#define GDR_BUCKET_FOLIAGE 1u			// Alpha tested and / or double sided (foliage, trees): no culling, discard
	#define GDR_BUCKET_TERRAIN 2u			// Terrain tiles: the mesh shader builds them from the height grid
	#define GDR_BUCKET_COUNT 3u

	// LightData::type
	#define GDR_LIGHT_DIRECTIONAL 0u
	#define GDR_LIGHT_POINT 1u
	#define GDR_MAX_LIGHTS 256u

	struct DebugData
	{
		uint debug_mode;
		uint cull_all_instances;
		uint freeze_culling_camera;
		uint pad3;

		uint use_frustum_culling;
		uint use_cone_culling;
		uint pad4;
		uint pad5;

		uint sort_mode; // 0 = no sorting, 1 = sort distance to camera (front to back), 2 = sort distance to camera (back to front)
		uint s1;
		uint s2;
		uint s3;

		DebugRenderMode debug_render_mode;
		uint d1;
		uint d2;
		uint d3;
	};

	struct DebugFeedbackData
	{
		uint meshlets_culled_total;
		uint meshlets_cullled_by_frustum;
		uint meshlets_culled_by_cone;
		uint pad;
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

	struct PerInstanceProcessingData
	{
		uint meshlets_to_process_count; // meshlet count * instance count
		uint meshlets_per_instance;
		uint meshlet_offset;
		uint instance_offset;

		uint bucket;					// GDR_BUCKET_* of every meshlet processed (and drawn) with this data
		uint bucket_output_offset;		// First element of the bucket's region of the visible meshlet list
		uint cone_culling;				// 0 for double sided buckets, whose back faces are visible
		uint pad0;
	};

	struct PerInstanceProcessingData_Output
	{
		uint instance_index;
		uint meshlet_index;
		float sort_key;
		uint pad;
	};

	struct InstanceData
	{
		row_major float4x4 model_matrix;
		float4 colour;

		uint model_index;
		uint pad1;
		uint pad2;
		uint pad3;
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

	// Texture indices are slices of the material texture array (GDR_NO_TEXTURE when unused)
	struct MaterialData
	{
		uint texture_diffuse_index;
		uint texture_specular_index;
		uint texture_normals_index;
		uint texture_opacity_index;

		float4 diffuse_colour;		// rgb = colour (gamma space, like instance colours), a = unused
		float4 specular;			// rgb = specular colour, a = Blinn-Phong exponent

		uint flags;					// GDR_MATERIAL_*
		float bump_strength;		// World space depth of GDR_MATERIAL_BUMP_MAP heights
		uint terrain_index;			// GDR_MATERIAL_TERRAIN: into the terrain buffer
		uint pad1;
	};

	// A terrain: a grid of (quads + 1)^2 heights, origin at its minimum x / z corner in model space
	struct TerrainData
	{
		float origin_x;
		float origin_z;
		float spacing;				// Model space distance between grid points
		float water_level;			// Model space height below which the terrain is flat water (-FLT_MAX for none)

		uint height_offset;			// First height in the terrain height buffer
		uint quads;					// Quads per side (a multiple of GDR_TERRAIN_TILE_QUADS)
		float snow_height;			// Height above which slopes turn to snow
		float max_height;

		float4 grass_colour;		// Colours in gamma space
		float4 rock_colour;
		float4 sand_colour;
		float4 snow_colour;
		float4 water_colour;
	};

	struct LightData
	{
		float4 position_range;		// World position, range (point lights)
		float4 colour;				// rgb = linear colour * intensity
		float4 direction;			// xyz = world direction the light travels (directional lights)
		uint type;					// GDR_LIGHT_*
		uint pad1;
		uint pad2;
		uint pad3;
	};

	// Lighting and atmosphere shared by the whole scene (one element)
	struct EnvironmentData
	{
		float4 sky_colour;			// Ambient light from above, linear
		float4 ground_colour;		// Ambient light from below, linear
		float4 fog_colour;			// rgb = linear colour, a = density (per world unit)
		float exposure;
		uint light_count;
		float fog_start;			// Distance before fog starts
		uint pad1;
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
		uint material_index;		// Into the material buffer
		uint meshlet_type;			// GDR_MESHLET_TYPE_*
		uint terrain_index;			// GDR_MESHLET_TYPE_TERRAIN: into the terrain buffer

		// Meshlet Culling Data
		float4 spherepos_xyz_radius_w;
		float4 cone_apex_xyz;
		float4 cone_axis_xyz_cone_cutoff_w;

		int cone_axis_s8xyz_cone_cutoff_s8w;
		uint terrain_tile_x;		// GDR_MESHLET_TYPE_TERRAIN: grid column / row of the tile's first vertex
		uint terrain_tile_z;
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