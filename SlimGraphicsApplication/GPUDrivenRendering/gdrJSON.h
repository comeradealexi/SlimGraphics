#pragma once
#include <nlohmann/json.hpp>
#include <DirectXMath.h>
#include <atomic>
#include <list>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace gdr
{
	constexpr std::string_view kINVALID_MODEL_NAME = "unknown";

	struct Float3
	{
		float x;
		float y;
		float z;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Float3, x, y, z);

	// A procedural terrain (a node's optional "terrain" component, rendered in place of a model). Heights are generated when
	// the scene loads and the GPU builds the vertices from them every frame (see gdrTerrain.h). Units are model space: the
	// node's transform places, rotates and scales the terrain like any model.
	struct TerrainDesc
	{
		float size = 256.0f;			// Width and depth, centred on the node
		std::uint32_t resolution = 252;		// Quads per side (rounded up to a multiple of the 7 quad tile size)
		float height = 24.0f;			// Height of the hills (or the white level of 'heightmap')
		std::uint32_t seed = 1;
		float feature_size = 80.0f;		// Wavelength of the largest hills
		std::uint32_t octaves = 5;
		float ridges = 0.4f;			// 0 = rolling hills, 1 = sharp ridged mountains
		std::string heightmap;			// Optional greyscale PNG (path relative to the working directory) instead of noise

		// Area kept flat at flat_height (for the scene's content), blending to the hills over flat_falloff
		float flat_x_min = 0.0f;
		float flat_z_min = 0.0f;
		float flat_x_max = 0.0f;
		float flat_z_max = 0.0f;
		float flat_falloff = 40.0f;
		float flat_height = -0.05f;

		bool water = false;				// Areas below water_level become a flat water surface
		float water_level = 0.0f;
		float snow_height = 1.0e6f;		// Height above which flat ground turns to snow

		// Colours (gamma space, like node colours)
		Float3 grass_colour = { 0.30f, 0.50f, 0.18f };
		Float3 rock_colour = { 0.42f, 0.39f, 0.36f };
		Float3 sand_colour = { 0.76f, 0.70f, 0.50f };
		Float3 snow_colour = { 0.95f, 0.96f, 1.00f };
		Float3 water_colour = { 0.10f, 0.32f, 0.45f };
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TerrainDesc, size, resolution, height, seed, feature_size, octaves, ridges, heightmap,
		flat_x_min, flat_z_min, flat_x_max, flat_z_max, flat_falloff, flat_height, water, water_level, snow_height,
		grass_colour, rock_colour, sand_colour, snow_colour, water_colour);

	// A light (a node's optional "light" component). Point lights sit at the node's world position; directional lights
	// shine along 'direction' rotated by the node's world transform.
	struct LightDesc
	{
		std::string type = "point";		// "point" or "directional"
		Float3 colour = { 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
		float range = 10.0f;			// Point lights fade to nothing at this distance
		Float3 direction = { 0.0f, -1.0f, 0.0f };
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LightDesc, type, colour, intensity, range, direction);

	struct SceneNode
	{
		Float3 position	= { 0.0f, 0.0f, 0.0f };
		Float3 rotation	= { 0.0f, 0.0f, 0.0f };
		Float3 scale	= { 1.0f, 1.0f, 1.0f };
		Float3 colour	= { 1.0f, 1.0f, 1.0f };
		std::string model_name = std::string(kINVALID_MODEL_NAME);
		std::list<SceneNode> children;

		// Optional components (serialised only when present)
		std::optional<TerrainDesc> terrain;
		std::optional<LightDesc> light;

		// Non-serialised fields
		SceneNode* parent = nullptr;
	};
	void to_json(nlohmann::json& j, const SceneNode& node);
	void from_json(const nlohmann::json& j, SceneNode& node);

	constexpr bool operator==(const SceneNode& lhs, const SceneNode& rhs)
	{
		// always stored in std::kist to simple pointer compare?
		return &lhs == &rhs;
	}

	// A looping camera path through a scene (optional "camera_track" key of a scene file's root object). The camera passes
	// through each point's position looking at its target; see CameraSpline for how it is interpolated.
	struct CameraTrackPoint
	{
		Float3 position = { 0.0f, 0.0f, 0.0f };
		Float3 target = { 0.0f, 0.0f, 1.0f };
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CameraTrackPoint, position, target);

	struct CameraTrack
	{
		float duration = 0.0f;		// Seconds per loop
		float near_plane = 0.1f;	// Clip planes used while the track plays (large scenes need a further far plane)
		float far_plane = 100.0f;
		std::vector<CameraTrackPoint> points;

		bool is_valid() const { return duration > 0.0f && points.size() >= 3 && near_plane > 0.0f && far_plane > near_plane; }
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CameraTrack, duration, near_plane, far_plane, points);

	// Ambient light, fog and exposure of a scene (optional "environment" key of a scene file's root object)
	struct SceneEnvironment
	{
		Float3 sky_colour = { 0.60f, 0.70f, 0.90f };	// Ambient light from above (gamma space)
		Float3 ground_colour = { 0.30f, 0.27f, 0.24f };	// Ambient light from below
		float ambient_intensity = 0.45f;
		Float3 fog_colour = { 0.72f, 0.78f, 0.88f };
		float fog_density = 0.0f;						// Per world unit (0 = no fog)
		float fog_start = 0.0f;							// Distance from the camera before fog starts
		float exposure = 1.0f;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SceneEnvironment, sky_colour, ground_colour, ambient_intensity, fog_colour, fog_density, fog_start, exposure);

	// Everything a scene file holds besides its node tree
	struct SceneProperties
	{
		CameraTrack camera_track;
		SceneEnvironment environment;
	};

	// Scene files: the root node's JSON object, plus optional "camera_track" and "environment" keys
	bool serialise(const SceneNode& node, const SceneProperties& properties, const char* file_path);
	bool deserialise(SceneNode& node, SceneProperties& properties, const char* file_path);

	// A camera track on its own (the PBR viewer's per scene tracks): a malformed file fails to load instead of aborting
	bool load_camera_track(const char* file_path, CameraTrack& track);
	bool save_camera_track(const CameraTrack& track, const char* file_path);

	// Recursively sets each child's parent pointer (needed after loading or copying a subtree).
	void set_parent_pointers(SceneNode& node, SceneNode* parent);
}