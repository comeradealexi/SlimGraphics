#pragma once
#include <nlohmann/json.hpp>
#include <DirectXMath.h>
#include <atomic>
#include <list>

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
		std::list<SceneNode> children;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SceneNode, position, rotation, scale, colour, model_name, children);

	constexpr bool operator==(const SceneNode& lhs, const SceneNode& rhs)
	{
		// always stored in std::kist to simple pointer compare?
		return &lhs == &rhs;
	}

	bool serialise(const SceneNode& node, const char* file_path);
	bool deserialise(SceneNode& node, const char* file_path);
}