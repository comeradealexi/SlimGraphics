#pragma once
#include <nlohmann/json.hpp>
#include <DirectXMath.h>

namespace gdr
{
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
		std::string model_name = "unknown";
		std::vector<SceneNode> children;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SceneNode, position, rotation, scale, colour, model_name, children);

	void serialise(const SceneNode& node, const char* file_path);
	void deserialise(SceneNode& node, const char* file_path);
}