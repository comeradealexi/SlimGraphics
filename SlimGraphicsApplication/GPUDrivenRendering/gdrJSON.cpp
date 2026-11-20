#include "gdrJSON.h"
#include <seEngineBasicFileIO.h>
#include <fstream>
#include <seEngine.h>

namespace gdr
{

	bool serialise(const SceneNode& node, const CameraTrack& camera_track, const char* file_path)
	{
		nlohmann::json j = node;
		if (camera_track.is_valid())
		{
			j["camera_track"] = camera_track;
		}
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

	// Checks every field NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SceneNode, ...) reads exists with the right type.
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

	bool deserialise(SceneNode& node, CameraTrack& camera_track, const char* file_path)
	{
		std::vector<uint8_t> file_data = se::BasicFileIO::load_file(file_path);
		if (file_data.size() == 0)
			return false;
		// The project builds without C++ exceptions, where a nlohmann parse/type error aborts the process.
		// Parse without exceptions and validate the structure before converting, so a malformed file just fails to load
		// (and leaves the current scene untouched).
		const nlohmann::json j = nlohmann::json::parse(file_data, nullptr, /*allow_exceptions*/ false);
		const bool has_camera_track = !j.is_discarded() && j.is_object() && j.contains("camera_track");
		if (j.is_discarded() || !is_valid_scene_node_json(j) || (has_camera_track && !is_valid_camera_track_json(j["camera_track"])))
		{
			seWriteLine("Failed to parse scene file %s", file_path);
			return false;
		}
		SceneNode loaded_node = j.get<SceneNode>();
		node = std::move(loaded_node);
		camera_track = has_camera_track ? j["camera_track"].get<CameraTrack>() : CameraTrack();

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