#pragma once
#include <nlohmann/json.hpp>
#include <DirectXMath.h>
#include <atomic>
#include <list>
#include <vector>

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

	struct SceneNode
	{
		Float3 position	= { 0.0f, 0.0f, 0.0f };
		Float3 rotation	= { 0.0f, 0.0f, 0.0f };
		Float3 scale	= { 1.0f, 1.0f, 1.0f };
		Float3 colour	= { 1.0f, 1.0f, 1.0f };
		std::string model_name = std::string(kINVALID_MODEL_NAME);
		std::list<SceneNode> children;

		// Non-serialised fields
		SceneNode* parent = nullptr;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SceneNode, position, rotation, scale, colour, model_name, children);

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

	// Scene files: the root node's JSON object, plus an optional camera track
	bool serialise(const SceneNode& node, const CameraTrack& camera_track, const char* file_path);
	bool deserialise(SceneNode& node, CameraTrack& camera_track, const char* file_path);

	// Recursively sets each child's parent pointer (needed after loading or copying a subtree).
	void set_parent_pointers(SceneNode& node, SceneNode* parent);
}