#pragma once
#include <array>
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

		void create_and_upload_to_gpu(sg::SharedPtr<sg::Device>& device, sg::Ptr<sg::UploadHeap>& upload_heap);
		void create(sg::SharedPtr<sg::Device>& device, sg::u32 element_count);
	};

	template <typename T>
	void gdr::GPUBuffer<T>::create_and_upload_to_gpu(sg::SharedPtr<sg::Device>& device, sg::Ptr<sg::UploadHeap>& upload_heap)
	{
		if (cpu_data.size() == 0)
			return;

		const size_t memory_size = cpu_data.size() * sizeof(T);
		sg::SharedPtr<sg::Memory> mem = device->allocate_memory(sg::MemoryType::GPUOptimal, sg::MemorySubType::Buffer, memory_size);
		buffer = device->create_buffer(mem, memory_size, sg::BufferType::GeneralDataBuffer, true);

		uav = device->create_unordered_access_view(buffer, sizeof(T), cpu_data.size());
		srv = device->create_shader_resource_view(buffer, sizeof(T), cpu_data.size());

		UploadHeap::Offset upload_offset = upload_heap->allocate_upload_memory(memory_size, 256);
		upload_heap->write_upload_memory(upload_offset, cpu_data.data(), memory_size);
		upload_heap->upload_to_buffer(buffer.get(), 0, upload_offset, memory_size);
	}

	template <typename T>
	void gdr::GPUBuffer<T>::create(sg::SharedPtr<sg::Device>& device, sg::u32 element_count)
	{
		const size_t memory_size = element_count * sizeof(T);
		sg::SharedPtr<sg::Memory> mem = device->allocate_memory(sg::MemoryType::GPUOptimal, sg::MemorySubType::Buffer, memory_size);
		buffer = device->create_buffer(mem, memory_size, sg::BufferType::GeneralDataBuffer, true);

		uav = device->create_unordered_access_view(buffer, sizeof(T), element_count);
		srv = device->create_shader_resource_view(buffer, sizeof(T), element_count);
	}

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
		// Per Node Data - (Can change per frame)
		GPUBuffer<ShaderStructs::InstanceData> instance_data;

		// Per Model Data - (Changes when models used changes)
		GPUBuffer<ShaderStructs::MeshletData>	meshlet_data;

		// Data buffers below should only change if model changes
		GPUBuffer<ShaderStructs::MaterialData>	material_data;
		GPUBuffer<ShaderStructs::ModelData>		model_data;
		GPUBuffer<Vertex>  vertex_data;
		GPUBuffer<sg::u32> meshlet_vertex_index;
		GPUBuffer<sg::u32> meshlet_primitive_index; // Packed Data - 1 tri per u32

		// GPU Write Only Buffers
		GPUBuffer<ShaderStructs::PerInstanceProcessingData_Output> gpu_pre_process_instances_output;
		GPUBuffer<std::array<sg::u32,4>> atomic_counter;
		GPUBuffer<std::array<sg::u32, 4>> indirect_arguments;

		// Record of all loaded models
		struct PerModelData
		{
			std::string name;
			sg::u32 material_count = 0;

			struct MeshParts
			{
				sg::u32 material_index = 0;
				sg::u32 vertex_data_offset = 0;
				sg::u32 meshlet_data_offset = 0;
				sg::u32 meshlet_vertex_index_offset = 0;
				sg::u32 meshlet_primitive_index_offset = 0;

				sg::u32 meshlet_count = 0;
			};
			std::vector<MeshParts> mesh_parts;
		};
		std::vector<PerModelData> models;

		const PerModelData& find_model(std::string_view name);
		void create_and_upload_to_gpu(sg::SharedPtr<sg::Device>& device, sg::Ptr<sg::UploadHeap>& upload_heap);
	};

	struct SceneGraphTraverser
	{

		struct PerModelData
		{
			DirectX::XMMATRIX model_matrix;
			const SceneNode* scene_node = nullptr;
		};

		std::map<std::string, std::vector<PerModelData>> model_map;

		void process_scene_graph(SceneNode& root_node);
	};

	class Scene
	{
		friend class Editor;
	public:
		Scene(sg::SharedPtr<sg::Device>& _device);
		void update(Ptr<UploadHeap>& upload_heap);
		void render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw);
		void load_model(std::string path, ShaderStructs::ModelData& gpu_model_data);
		void load_gpu_data(SceneGraphTraverser& scene_graph, Ptr<UploadHeap>& upload_heap);


	private:
		SceneNode root_scene_node;
		Editor editor;

		sg::SharedPtr<sg::Device> device;

		ModelBuffers model_buffers;
		struct PerInstanceRenderInfo
		{
			sg::u32 start_index;
			sg::u32 count;
			sg::u32 meshlets_per_instance;
		};
		std::vector<PerInstanceRenderInfo> per_instance_render_list;

		sg::SharedPtr<sg::IndirectArguments> mesh_shader_indirect_arguments;

		sg::SharedPtr<sg::Pipeline> per_instance_compute_pipeline;
		sg::SharedPtr<sg::Pipeline> build_indirect_arguments_pipeline;
		sg::SharedPtr<sg::Pipeline> mesh_render_pipeline;
	};
}