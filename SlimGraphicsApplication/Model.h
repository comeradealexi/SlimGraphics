#pragma once
#include <sgTypes.h>
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
		DirectX::XMFLOAT3 Color;
		DirectX::XMFLOAT2 UV;
		DirectX::XMFLOAT3 Normal;
		DirectX::XMFLOAT3 Tangent;

		static inline sg::InputLayout::Desc make_input_layout(sg::u32* out_stride = nullptr)
		{
			using namespace sg;
			InputLayout::Desc d = {};

			sg::u32 size_offset = 0;
			d.elements[d.num_elements++] = InputLayout::ElementDesc({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, size_offset, InputLayout::InputClassification::PerVertex, 0 });
			size_offset += sizeof(Position);

			d.elements[d.num_elements++] = InputLayout::ElementDesc({ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, size_offset, InputLayout::InputClassification::PerVertex, 0 });
			size_offset += sizeof(Color);

			d.elements[d.num_elements++] = InputLayout::ElementDesc({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, size_offset, InputLayout::InputClassification::PerVertex, 0 });
			size_offset += sizeof(UV);

			d.elements[d.num_elements++] = InputLayout::ElementDesc({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, size_offset, InputLayout::InputClassification::PerVertex, 0 });
			size_offset += sizeof(Normal);

			d.elements[d.num_elements++] = InputLayout::ElementDesc({ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, size_offset, InputLayout::InputClassification::PerVertex, 0 });
			size_offset += sizeof(Tangent);

			if (out_stride)
				*out_stride = size_offset;

			return d;
		}
	};

public:
	Model(sg::Device* device, UploadHeap* upload_heap, const std::string_view file);

	struct MeshPart
	{
		sg::u32 material_index = 0;
		sg::u32 vb_offset = 0; // Offset in stride count (not bytes)
		sg::u32 ib_offset = 0; // Offset in element count (not bytes)
		sg::u32 draw_count = 0;
	};

	struct Material
	{

	};

	void SetPipeline(sg::Ptr<sg::Pipeline> new_pipeline);
	void Render(sg::CommandList* command_list);

private:
	sg::Ptr<sg::Pipeline> pipeline;
	sg::SharedPtr<sg::Buffer> vb;
	sg::SharedPtr<sg::Buffer> ib;
	sg::VertexBufferView vbv;
	sg::IndexBufferView ibv;
	std::vector<MeshPart> mesh_parts;
	std::vector<Material> materials;
};

