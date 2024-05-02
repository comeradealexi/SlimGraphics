#pragma once
#include <sgPlatformInclude.h>
#include <seEngineBasicFileIO.h>
#include <DirectXMath.h>
#include "UploadHeap.h"

class Model
{
public:
	struct Vertex
	{
		DirectX::XMFLOAT3 Position;
		DirectX::XMFLOAT2 UV;
		DirectX::XMFLOAT3 Normal;
		DirectX::XMFLOAT3 Tangent;

		static inline sg::InputLayout::Desc make_input_layout()
		{
			using namespace sg;
			InputLayout::Desc d = {};

			u32 size_offset = 0;
			d.elements[d.num_elements++] = InputLayout::ElementDesc({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, size_offset, InputLayout::InputClassification::PerVertex, 0 });
			size_offset += sizeof(Position);

			d.elements[d.num_elements++] = InputLayout::ElementDesc({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, size_offset, InputLayout::InputClassification::PerVertex, 0 });
			size_offset += sizeof(UV);

			d.elements[d.num_elements++] = InputLayout::ElementDesc({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, size_offset, InputLayout::InputClassification::PerVertex, 0 });
			size_offset += sizeof(Normal);

			d.elements[d.num_elements++] = InputLayout::ElementDesc({ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, size_offset, InputLayout::InputClassification::PerVertex, 0 });
			size_offset += sizeof(Tangent);

			return d;
		}
	};

public:
	Model(sg::Device* device, UploadHeap* upload_heap, const std::string_view file);

	struct MeshPart
	{
		uint32_t material_index = 0;
		uint32_t vb_offset = 0;
		uint32_t ib_offset = 0;
		uint32_t draw_count = 0;
	};

	struct Material
	{

	};

private:
	sg::Ptr<sg::Pipeline> pipeline;
	sg::SharedPtr<sg::Buffer> vb;
	sg::SharedPtr<sg::Buffer> ib;
	std::vector<MeshPart> mesh_parts;
	std::vector<Material> materials;
};

