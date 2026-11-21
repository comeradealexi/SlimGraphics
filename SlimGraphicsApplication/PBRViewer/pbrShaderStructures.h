#ifndef PBR_SHADER_STRUCTURES_H
#define PBR_SHADER_STRUCTURES_H

// Structures and constants shared by the PBR viewer's C++ and HLSL. Buffer layouts are D3D layouts: structured buffers are
// tightly packed, constant buffers follow HLSL packing (everything here is laid out in 16 byte rows so both agree).

#ifdef __cplusplus
#include <DirectXMath.h>
#include <cstdint>
namespace pbr
{
	namespace gpu
	{
		using float2 = DirectX::XMFLOAT2;
		using float3 = DirectX::XMFLOAT3;
		using float4 = DirectX::XMFLOAT4;
		using float4x4 = DirectX::XMFLOAT4X4;
		using uint = uint32_t;
		using uint2 = DirectX::XMUINT2;
		using uint4 = DirectX::XMUINT4;
#define row_major
#endif

#define PBR_MESHLET_MAX_VERTICES 64
#define PBR_MESHLET_MAX_TRIANGLES 124
#define PBR_TASK_MESHLETS 32				// Meshlets per amplification shader group (one per thread)
#define PBR_MAX_DISPATCH 65535u				// Per dimension limit of DispatchMesh / Dispatch (work is spread over x and y)
#define PBR_NO_TEXTURE 0xFFFFFFFFu

// Material::flags
#define PBR_MATERIAL_ALPHA_MASK 1u			// Alpha tested against alpha_cutoff
#define PBR_MATERIAL_DOUBLE_SIDED 2u		// No back face culling; back faces are shaded with the flipped normal
#define PBR_MATERIAL_BASE_COLOUR_SRGB 4u	// The base colour texture holds sRGB values in a UNORM format (decoded in shaders)
#define PBR_MATERIAL_EMISSIVE_SRGB 8u		// Likewise for the emissive texture
#define PBR_MATERIAL_LEGACY_SPECULAR 16u	// metal_rough_texture is a Phong specular intensity map (roughness derived from it)

// Render buckets: each has its own pipeline state, amplification / mesh / pixel shaders and indirect dispatch
#define PBR_BUCKET_OPAQUE 0u				// Back face culled, no pixel shader discard (keeps early depth testing)
#define PBR_BUCKET_MASKED 1u				// Alpha tested and / or double sided
#define PBR_BUCKET_COUNT 2u

// Instance::flags
#define PBR_INSTANCE_BUCKET_MASK 1u			// Bucket of the instance's material
#define PBR_INSTANCE_MIRRORED 2u			// Negative determinant: triangle winding is reversed

// Culling phases (two phase occlusion culling with per instance and per meshlet visibility from the previous frame)
#define PBR_PHASE_EARLY 0u					// Draws what was visible last frame
#define PBR_PHASE_LATE 1u					// Tests everything else against the HZB of the early pass

// Debug views (FrameConstants::debug_view)
#define PBR_DEBUG_NONE 0u
#define PBR_DEBUG_ALBEDO 1u
#define PBR_DEBUG_NORMALS 2u
#define PBR_DEBUG_GEOMETRIC_NORMALS 3u
#define PBR_DEBUG_ROUGHNESS 4u
#define PBR_DEBUG_METALLIC 5u
#define PBR_DEBUG_AO 6u
#define PBR_DEBUG_EMISSIVE 7u
#define PBR_DEBUG_MOTION 8u
#define PBR_DEBUG_DEPTH 9u
#define PBR_DEBUG_INSTANCES 10u
#define PBR_DEBUG_MESHLETS 11u
#define PBR_DEBUG_TRIANGLES 12u
#define PBR_DEBUG_SHADOW_RAW 13u
#define PBR_DEBUG_SHADOW 14u
#define PBR_DEBUG_DIFFUSE_RAW 15u
#define PBR_DEBUG_DIFFUSE 16u
#define PBR_DEBUG_SPECULAR_RAW 17u
#define PBR_DEBUG_SPECULAR 18u
#define PBR_DEBUG_DIRECT 19u
#define PBR_DEBUG_INDIRECT 20u
#define PBR_DEBUG_HISTORY_LENGTH 21u
#define PBR_DEBUG_VARIANCE 22u
#define PBR_DEBUG_HZB 23u
#define PBR_DEBUG_RADIANCE_CACHE 24u
#define PBR_DEBUG_WHITE_FURNACE 25u
#define PBR_DEBUG_COUNT 26u

// FrameConstants::flags
#define PBR_FLAG_FRUSTUM_CULLING 1u
#define PBR_FLAG_CONE_CULLING 2u
#define PBR_FLAG_OCCLUSION_CULLING 4u
#define PBR_FLAG_SHADOWS 8u
#define PBR_FLAG_DIFFUSE_GI 16u
#define PBR_FLAG_SPECULAR 32u
#define PBR_FLAG_RADIANCE_CACHE 64u
#define PBR_FLAG_SKY_LIGHTING 128u
#define PBR_FLAG_EMISSIVE 256u
#define PBR_FLAG_TEXTURES 512u
#define PBR_FLAG_NORMAL_MAPS 1024u
#define PBR_FLAG_ALPHA_TEST 2048u
#define PBR_FLAG_SUN_DISK 4096u

// Earth's atmosphere (metres; Bruneton / Hillaire parameters), shared by the sky shader and the CPU sun transmittance
#define PBR_EARTH_RADIUS 6360000.0f
#define PBR_ATMOSPHERE_RADIUS 6460000.0f
#define PBR_RAYLEIGH_SCALE_HEIGHT 8000.0f
#define PBR_MIE_SCALE_HEIGHT 1200.0f
#define PBR_RAYLEIGH_SCATTERING_R 5.802e-6f
#define PBR_RAYLEIGH_SCATTERING_G 13.558e-6f
#define PBR_RAYLEIGH_SCATTERING_B 33.1e-6f
#define PBR_MIE_SCATTERING 3.996e-6f
#define PBR_MIE_EXTINCTION 4.440e-6f
#define PBR_OZONE_ABSORPTION_R 0.650e-6f
#define PBR_OZONE_ABSORPTION_G 1.881e-6f
#define PBR_OZONE_ABSORPTION_B 0.085e-6f
#define PBR_OZONE_CENTRE 25000.0f
#define PBR_OZONE_WIDTH 15000.0f
#define PBR_VIEWER_ALTITUDE 200.0f

	struct Vertex
	{
		float3 position;
		float3 normal;
		float4 tangent;					// xyz, w = bitangent sign
		float2 uv;
	};

	struct Material
	{
		float4 base_colour;				// Linear rgb, a = alpha
		float3 emissive;				// Linear, multiplied by the emissive texture
		float alpha_cutoff;

		float metallic;
		float roughness;
		float normal_scale;
		float occlusion_strength;

		uint base_colour_texture;		// Bindless descriptor indices (PBR_NO_TEXTURE when unused)
		uint normal_texture;
		uint metal_rough_texture;		// glTF / ORM packing: g = roughness, b = metallic (r = occlusion when occlusion_texture is the same)
		uint emissive_texture;

		uint occlusion_texture;			// r = ambient occlusion
		uint flags;						// PBR_MATERIAL_*
		float specular;					// Dielectric F0 scale (KHR_materials_specular style, 1 = 4%)
		uint opacity_texture;			// r = opacity; PBR_NO_TEXTURE: the base colour texture's alpha
	};

	struct Mesh
	{
		uint index_offset;				// Into the index buffer (indices are absolute vertex buffer indices)
		uint index_count;
		uint meshlet_offset;			// Into the meshlet buffer
		uint meshlet_count;

		uint material;
		uint vertex_offset;
		uint vertex_count;
		float uv_density;				// Texture coordinate area per unit of object space area (texture level of detail of ray hits)

		float4 sphere;					// Object space bounding sphere (centre, radius)
		float4 aabb_min;
		float4 aabb_max;
	};

	struct Meshlet
	{
		uint vertex_offset;				// Into the meshlet vertex list (absolute vertex buffer indices)
		uint triangle_offset;			// Into the packed triangle list (one uint per triangle: 3 x 8 bit local indices)
		uint vertex_count;
		uint triangle_count;

		float4 sphere;					// Object space (centre, radius)
		float4 cone_apex;				// xyz = apex, w = cone cutoff (cos of the cone's half angle, negated test)
		float4 cone_axis;				// xyz
	};

	struct Instance
	{
		row_major float4x4 world;
		row_major float4x4 normal_matrix;	// Inverse transpose of world's 3x3 (rows 0..2)
		row_major float4x4 previous_world;
		float4 world_sphere;			// World space bounding sphere

		uint mesh;
		uint material;
		uint meshlet_visibility_offset;	// First bit of the instance's meshlets in the meshlet visibility bit array
		uint flags;						// PBR_INSTANCE_*
	};

	// Per frame constants (b0)
	struct FrameConstants
	{
		row_major float4x4 view;
		row_major float4x4 projection;					// Jittered, reverse Z, infinite far plane
		row_major float4x4 view_projection;				// Jittered
		row_major float4x4 view_projection_no_jitter;
		row_major float4x4 previous_view_projection_no_jitter;
		row_major float4x4 inverse_view_projection;		// Of the jittered view projection
		row_major float4x4 culling_view_projection;		// Unjittered, of the (optionally frozen) culling camera

		float4 camera_position;				// xyz, w = near plane
		float4 previous_camera_position;
		float4 culling_camera_position;		// xyz, w = near plane
		float4 frustum_planes[6];			// Of the culling camera, normals point inwards

		float2 resolution;
		float2 inverse_resolution;

		float2 jitter;						// Sub pixel jitter of this frame, in pixels
		float2 previous_jitter;

		float4 sun_direction;				// xyz = towards the sun, w = cos(sun angular radius)
		float4 sun_illuminance;				// rgb = illuminance at the ground (after the atmosphere), w = sun angular radius (radians)
		float4 sun_disk_radiance;			// rgb = radiance of the sun disk seen from the ground

		uint frame_index;
		uint flags;							// PBR_FLAG_*
		uint debug_view;					// PBR_DEBUG_*
		uint hzb_mip_count;

		float2 hzb_size;					// Size of HZB mip 0
		float exposure;						// Multiplier applied before tone mapping
		float emissive_scale;

		float roughness_min;				// Clamp (path tracer and real time) avoiding singular specular lobes
		float roughness_override;			// < 0: off
		float metallic_override;			// < 0: off
		float texture_lod_bias;

		float3 albedo_override;				// x < 0: off
		float sky_intensity;

		float ray_offset;					// World space ray origin offset scale
		float max_ray_distance;
		float firefly_clamp;				// Maximum luminance of an indirect sample (0 = off)
		float scene_scale;					// World units per metre

		float4 radiance_cache;				// x = cell size at 1 unit distance, y = minimum cell size, z = blend, w = capacity (entries)
		float4 sky_parameters;				// x = ground albedo, y = sky texture width, z = height, w = alpha test level bias of ray hits
		uint4 alpha_test_limits;			// Most alpha tested candidates a ray tests: x = primary shadow rays, y = other real time rays
		float4 emissive_lights;				// x = emissive triangle count, y = total sampling weight, z = 1 if sampled (else found by rays), w = candidates per sample
	};

	// An emissive triangle of the light sampling alias table (Vose): column i is picked uniformly, then i itself with
	// 'probability', else 'alias'. Triangles are weighted by luminance(material emissive) * world area.
	struct EmissiveTriangle
	{
		uint instance;
		uint primitive;
		float probability;
		uint alias;
	};

	// Culling and visibility buffer passes (b1)
	struct GeometryPassConstants
	{
		uint phase;							// PBR_PHASE_*
		uint bucket;						// PBR_BUCKET_* drawn by the amplification / mesh / pixel shaders
		uint instance_count;
		uint pad0;

		uint4 task_region_offsets;			// First task of each bucket's region of the task buffer
		uint4 task_region_sizes;			// Capacity of each region
	};

	// Culling statistics (a buffer of uints), read back for the statistics window and tests
	#define PBR_STAT_INSTANCES_FRUSTUM_CULLED 0
	#define PBR_STAT_INSTANCES_OCCLUSION_CULLED 1
	#define PBR_STAT_MESHLETS_FRUSTUM_CULLED 2
	#define PBR_STAT_MESHLETS_CONE_CULLED 3
	#define PBR_STAT_MESHLETS_OCCLUSION_CULLED 4
	#define PBR_STAT_MESHLETS_DRAWN_EARLY 5
	#define PBR_STAT_MESHLETS_DRAWN_LATE 6
	#define PBR_STAT_TRIANGLES_DRAWN 7
	#define PBR_STAT_COUNT 8

	// Generic constants of the full screen compute passes (b1)
	struct PassConstants
	{
		uint4 u0;
		uint4 u1;
		uint4 u2;
		uint4 u3;
		float4 f0;
		float4 f1;
		float4 f2;
		float4 f3;
	};

#ifdef __cplusplus
	} // namespace gpu
} // namespace pbr
#undef row_major
#endif

#endif
