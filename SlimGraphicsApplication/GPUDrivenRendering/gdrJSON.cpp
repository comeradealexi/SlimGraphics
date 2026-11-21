#include "gdrJSON.h"
#include <seEngineBasicFileIO.h>
#include <fstream>
#include <seEngine.h>

namespace gdr
{
	void to_json(nlohmann::json& j, const SceneNode& node)
	{
		j = nlohmann::json{ { "position", node.position }, { "rotation", node.rotation }, { "scale", node.scale }, { "colour", node.colour },
			{ "model_name", node.model_name }, { "children", node.children } };
		if (node.terrain) j["terrain"] = *node.terrain;
		if (node.light) j["light"] = *node.light;
	}

	void from_json(const nlohmann::json& j, SceneNode& node)
	{
		j.at("position").get_to(node.position);
		j.at("rotation").get_to(node.rotation);
		j.at("scale").get_to(node.scale);
		j.at("colour").get_to(node.colour);
		j.at("model_name").get_to(node.model_name);
		j.at("children").get_to(node.children);
		node.terrain = j.contains("terrain") ? std::optional<TerrainDesc>(j["terrain"].get<TerrainDesc>()) : std::nullopt;
		node.light = j.contains("light") ? std::optional<LightDesc>(j["light"].get<LightDesc>()) : std::nullopt;
	}

	bool serialise(const SceneNode& node, const SceneProperties& properties, const char* file_path)
	{
		nlohmann::json j = node;
		if (properties.camera_track.is_valid())
		{
			j["camera_track"] = properties.camera_track;
		}
		j["environment"] = properties.environment;
		std::ofstream os(file_path, std::ios::out | std::ios::trunc);
		seAssert(os.good(), "Failed To Write File %s", file_path);
		if (os.good())
		{
			os << j;
			os.close();
			return true;
		}
		return false;
	}

	static bool is_valid_float3_json(const nlohmann::json& j)
	{
		return j.is_object() && j.contains("x") && j["x"].is_number() && j.contains("y") && j["y"].is_number() && j.contains("z") && j["z"].is_number();
	}

	// Whether every key of 'j' exists in 'reference' (the JSON of a default constructed object) with a compatible type. Used for
	// the components read with NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT: missing keys take their defaults, but an unknown
	// key would be silently ignored and a mistyped one aborts the process (no exceptions).
	static bool matches_schema(const nlohmann::json& j, const nlohmann::json& reference, const char* what)
	{
		if (!j.is_object())
		{
			seWriteLine("Scene file: \"%s\" must be an object", what);
			return false;
		}
		for (auto it = j.begin(); it != j.end(); ++it)
		{
			if (!reference.contains(it.key()))
			{
				seWriteLine("Scene file: unknown key \"%s\" in \"%s\"", it.key().c_str(), what);
				return false;
			}
			const nlohmann::json& expected = reference[it.key()];
			// Nested objects (Float3) are read with at(), so they need every key
			const bool compatible = (expected.is_number() && it.value().is_number()) ||
				(expected.is_object() && it.value().is_object() && it.value().size() == expected.size() && matches_schema(it.value(), expected, it.key().c_str())) ||
				(!expected.is_number() && !expected.is_object() && expected.type() == it.value().type());
			if (!compatible)
			{
				seWriteLine("Scene file: \"%s\" in \"%s\" has the wrong type", it.key().c_str(), what);
				return false;
			}
		}
		return true;
	}

	// Checks every field from_json(SceneNode) reads exists with the right type.
	static bool is_valid_scene_node_json(const nlohmann::json& j)
	{
		if (!j.is_object())
			return false;
		for (const char* field : { "position", "rotation", "scale", "colour" })
		{
			if (!j.contains(field) || !is_valid_float3_json(j[field]))
				return false;
		}
		if (!j.contains("model_name") || !j["model_name"].is_string())
			return false;
		if (!j.contains("children") || !j["children"].is_array())
			return false;
		if (j.contains("terrain") && !matches_schema(j["terrain"], nlohmann::json(TerrainDesc()), "terrain"))
			return false;
		if (j.contains("light") && !matches_schema(j["light"], nlohmann::json(LightDesc()), "light"))
			return false;
		for (const nlohmann::json& child : j["children"])
		{
			if (!is_valid_scene_node_json(child))
				return false;
		}
		return true;
	}

	// Checks the optional camera track has every field NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CameraTrack, ...) reads
	static bool is_valid_camera_track_json(const nlohmann::json& j)
	{
		if (!j.is_object())
			return false;
		for (const char* field : { "duration", "near_plane", "far_plane" })
		{
			if (!j.contains(field) || !j[field].is_number())
				return false;
		}
		if (!j.contains("points") || !j["points"].is_array())
			return false;
		for (const nlohmann::json& point : j["points"])
		{
			if (!point.is_object() || !point.contains("position") || !is_valid_float3_json(point["position"]) ||
				!point.contains("target") || !is_valid_float3_json(point["target"]))
				return false;
		}
		return true;
	}

	bool load_camera_track(const char* file_path, CameraTrack& track)
	{
		std::vector<uint8_t> file_data = se::BasicFileIO::load_file(file_path);
		if (file_data.size() == 0)
			return false;
		const nlohmann::json j = nlohmann::json::parse(file_data, nullptr, /*allow_exceptions*/ false);
		if (j.is_discarded() || !is_valid_camera_track_json(j))
		{
			seWriteLine("Failed to parse camera track %s", file_path);
			return false;
		}
		track = j.get<CameraTrack>();
		return true;
	}

	bool save_camera_track(const CameraTrack& track, const char* file_path)
	{
		std::ofstream os(file_path, std::ios::out | std::ios::trunc);
		if (!os.good())
			return false;
		os << nlohmann::json(track).dump(1, '\t') << "\n";
		return os.good();
	}

	bool deserialise(SceneNode& node, SceneProperties& properties, const char* file_path)
	{
		std::vector<uint8_t> file_data = se::BasicFileIO::load_file(file_path);
		if (file_data.size() == 0)
			return false;
		// The project builds without C++ exceptions, where a nlohmann parse/type error aborts the process.
		// Parse without exceptions and validate the structure before converting, so a malformed file just fails to load
		// (and leaves the current scene untouched).
		const nlohmann::json j = nlohmann::json::parse(file_data, nullptr, /*allow_exceptions*/ false);
		const bool valid_object = !j.is_discarded() && j.is_object();
		const bool has_camera_track = valid_object && j.contains("camera_track");
		const bool has_environment = valid_object && j.contains("environment");
		if (!valid_object || !is_valid_scene_node_json(j) || (has_camera_track && !is_valid_camera_track_json(j["camera_track"])) ||
			(has_environment && !matches_schema(j["environment"], nlohmann::json(SceneEnvironment()), "environment")))
		{
			seWriteLine("Failed to parse scene file %s", file_path);
			return false;
		}
		SceneNode loaded_node = j.get<SceneNode>();
		node = std::move(loaded_node);
		properties.camera_track = has_camera_track ? j["camera_track"].get<CameraTrack>() : CameraTrack();
		properties.environment = has_environment ? j["environment"].get<SceneEnvironment>() : SceneEnvironment();

		// Set parent pointers
		set_parent_pointers(node, nullptr);

		return true;
	}

	void set_parent_pointers(SceneNode& scene_node, SceneNode* parent)
	{
		scene_node.parent = parent;
		for (SceneNode& child_node : scene_node.children)
		{
			set_parent_pointers(child_node, &scene_node);
		}
	}
}
