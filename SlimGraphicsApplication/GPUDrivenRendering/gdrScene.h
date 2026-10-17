#pragma once
#include <sgPlatformInclude.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <unordered_map>
#include "sgUploadHeap.h"
#include "gdrShaderStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "DebugDraw.h"
#include "gdrJSON.h"
#include "gdrEditor.h"
#include "gdrModel.h"

namespace gdr
{
	using sg::Ptr;
	using sg::UploadHeap;

	template <typename T>
	struct GPUBuffer
	{
		std::vector<T>				cpu_data;
		sg::SharedPtr<sg::Buffer>	buffer;
		sg::UnorderedAccessView		uav;
		sg::ShaderResourceView		srv;

		void create_and_upload_to_gpu(sg::Ptr<sg::UploadHeap>& upload_heap);
	};

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

	struct ModelBuffers
	{
		// Per Node Data
		GPUBuffer<ShaderStructs::InstanceData> node_data;

		// Per Model Data
		GPUBuffer<ShaderStructs::MeshletData>	meshlet_data;

		// Data buffers below should only change if model changes
		GPUBuffer<ShaderStructs::MaterialData>	material_data;
		GPUBuffer<Vertex>  vertex_data;
		GPUBuffer<sg::u32> meshlet_vertex_index;
		GPUBuffer<sg::u32> meshlet_primitive_index; // Packed Data - 1 tri per u32

		// Record of all loaded models
		struct PerModelData
		{
			std::string name;
			sg::u32 material_count = 0;
			sg::u32 meshlet_count = 0;
			sg::u32 vertex_data_offset = 0;
			sg::u32 meshlet_vertex_index_offset = 0;
			sg::u32 meshlet_primitive_index_offset = 0;

		};
		std::vector<PerModelData> models;
	};

	struct SceneGraphTraverser
	{
		std::set<std::string> unique_model_paths;

		struct PerModelData
		{
			DirectX::XMMATRIX model_matrix;
			const SceneNode* scene_node = nullptr;
		};
		std::vector<PerModelData> nodes;

		void process_scene_graph(SceneNode& root_node);
	};

	class Scene
	{
		friend class Editor;
	public:
		Scene(sg::SharedPtr<sg::Device>& _device);
		void update(Ptr<UploadHeap>& upload_heap);
		void render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw);
		void load_gpu_data(SceneGraphTraverser& scene_graph, Ptr<UploadHeap>& upload_heap);

	private:
		SceneNode root_scene_node;
		Editor editor;

		sg::SharedPtr<sg::Device> device;

		ModelBuffers model_buffers;
	};
}