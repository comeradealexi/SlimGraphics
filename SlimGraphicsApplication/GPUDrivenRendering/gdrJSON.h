#pragma once
#include <nlohmann/json.hpp>
#include <DirectXMath.h>

namespace gdr
{
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DirectX::XMFLOAT3, x, y, z);

	struct SceneNode
	{
		DirectX::XMFLOAT3 position	= { 0.0f, 0.0f, 0.0f };
		DirectX::XMFLOAT3 rotation	= { 0.0f, 0.0f, 0.0f };
		DirectX::XMFLOAT3 scale		= { 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT3 colour	= { 1.0f, 1.0f, 1.0f };
		std::string model_name = "unknown";
		std::vector<SceneNode> children;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SceneNode, position, rotation, scale, colour, model_name, children);

	void serialise(const SceneNode& node, const char* file_path);
	void deserialise(SceneNode& node, const char* file_path);
}