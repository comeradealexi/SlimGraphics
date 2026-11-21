#pragma once
#include <array>
#include <sgPlatformInclude.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <unordered_map>
#include <set>
#include <map>
#include <bit>
#include "sgUploadHeap.h"
#include "gdrShaderStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "DebugDraw.h"
#include "gdrJSON.h"
#include "gdrEditor.h"
#include "gdrModel.h"
#include "gdrSort.h"
#include "gdrHZB.h"
#include "gdrMaterials.h"
#include "gdrTerrain.h"

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
		sg::u32						allocated_memory_size = 0;

		void create_and_upload_to_gpu(sg::SharedPtr<sg::Device>& device, sg::Ptr<sg::UploadHeap>& upload_heap);
		void create(sg::SharedPtr<sg::Device>& device, sg::u32 element_count, bool power_of_two_elements = false);

		sg::u32 get_element_count() const
		{
			if (allocated_memory_size == 0) return 0;
			return allocated_memory_size / sizeof(T);
		}
	};

	template <typename T>
	void gdr::GPUBuffer<T>::create_and_upload_to_gpu(sg::SharedPtr<sg::Device>& device, sg::Ptr<sg::UploadHeap>& upload_heap)
	{
		if (cpu_data.size() == 0)
			return;

		const sg::u32 memory_size = static_cast<sg::u32>(cpu_data.size() * sizeof(T));

		// Only reallocate when the existing buffer is too small. Data that changes every frame (e.g. instance data)
		// then reuses the same GPU buffer instead of allocating a new one per frame.
		if (!buffer || allocated_memory_size < memory_size)
		{
			allocated_memory_size = memory_size;
			sg::SharedPtr<sg::Memory> mem = device->allocate_memory(sg::MemoryType::GPUOptimal, sg::MemorySubType::Buffer, memory_size);
			buffer = device->create_buffer(mem, memory_size, sg::BufferType::GeneralDataBuffer, true);

			uav = device->create_unordered_access_view(buffer, sizeof(T), cpu_data.size());
			srv = device->create_shader_resource_view(buffer, sizeof(T), cpu_data.size());
		}

		UploadHeap::Offset upload_offset = upload_heap->allocate_upload_memory(memory_size, 256);
		if (upload_offset == UploadHeap::INVALID_OFFSET)
		{
			return;
		}
		upload_heap->write_upload_memory(upload_offset, cpu_data.data(), memory_size);
		upload_heap->upload_to_buffer(buffer.get(), 0, upload_offset, memory_size);
	}

	template <typename T>
	void gdr::GPUBuffer<T>::create(sg::SharedPtr<sg::Device>& device, sg::u32 element_count, bool power_of_two_elements)
	{
		// Power of two buffers are bitonic sorted in tiles of 32, so never make them smaller than one tile.
		const size_t scaled_element_count = power_of_two_elements ? std::max<size_t>(std::bit_ceil(std::max<sg::u32>(element_count, 1u)), 32) : std::max<sg::u32>(element_count, 1u);
		const sg::u32 memory_size = static_cast<sg::u32>(scaled_element_count * sizeof(T));
		if (allocated_memory_size < memory_size)
		{
			allocated_memory_size = memory_size;
			sg::SharedPtr<sg::Memory> mem = device->allocate_memory(sg::MemoryType::GPUOptimal, sg::MemorySubType::Buffer, memory_size);
			buffer = device->create_buffer(mem, memory_size, sg::BufferType::GeneralDataBuffer, true);

			uav = device->create_unordered_access_view(buffer, sizeof(T), scaled_element_count);
			srv = device->create_shader_resource_view(buffer, sizeof(T), scaled_element_count);
		}
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
		GPUBuffer<ShaderStructs::TerrainData> terrain_data;
		GPUBuffer<float> terrain_heights;			// Height grids of every terrain (TerrainData::height_offset)
		MaterialTextures material_textures;			// Texture array slices referenced by material_data

		// GPU Write Only Buffers
		GPUBuffer<ShaderStructs::PerInstanceProcessingData_Output> gpu_pre_process_instances_output;
		GPUBuffer<std::array<sg::u32, 4>> atomic_counter;		// x = visible meshlets of each render bucket
		GPUBuffer<std::array<sg::u32, 4>> indirect_arguments;	// DispatchMesh arguments of each render bucket

		// Record of all loaded models
		struct ModelData
		{
			std::string name;
			sg::u32 material_count = 0;

			struct MeshPart
			{
				sg::u32 material_index = 0;
				sg::u32 bucket = GDR_BUCKET_OPAQUE;		// Render bucket of the part's material (bucket_of_material)
				sg::u32 vertex_data_offset = 0;
				sg::u32 meshlet_data_offset = 0;
				sg::u32 meshlet_vertex_index_offset = 0;
				sg::u32 meshlet_primitive_index_offset = 0;
				sg::u32 meshlet_count = 0;

				// Culling Data
				DirectX::XMFLOAT3 bounding_box_min = { FLT_MAX, FLT_MAX, FLT_MAX };
				DirectX::XMFLOAT3 bounding_box_max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
				DirectX::BoundingSphere sphere = {};
				DirectX::BoundingBox aabb = {};
				DirectX::BoundingOrientedBox obb = {};
			};
			std::vector<MeshPart> mesh_parts;
		};
		std::vector<ModelData> models;

		// Returns nullptr when the model is not loaded (e.g. it failed to import)
		const ModelData* find_model(std::string_view name);
		void create_and_upload_to_gpu(sg::SharedPtr<sg::Device>& device, sg::Ptr<sg::UploadHeap>& upload_heap);
	};

	struct SceneGraphTraverser
	{

		struct InstanceData
		{
			DirectX::XMMATRIX model_matrix;
			Float3 colour;
			const SceneNode* scene_node = nullptr;
		};

		// Instances per model name (terrains appear under terrain_model_name(desc))
		std::map<std::string, std::vector<InstanceData>> model_instance_map;
		// Terrain description of each terrain model name
		std::map<std::string, const TerrainDesc*> terrains;

		struct LightInstance
		{
			DirectX::XMMATRIX world_matrix;
			const LightDesc* light = nullptr;
		};
		std::vector<LightInstance> lights;

		void process_scene_graph(SceneNode& root_node);
	};

	class Scene
	{
		friend class Editor;
	public:
		Scene(sg::SharedPtr<sg::Device>& _device, sg::SharedPtr<sg::Texture> depth_texture);
		void create_pipelines();
		void imgui();
		void update(Ptr<UploadHeap>& upload_heap);
		void render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw, sg::ShaderResourceView depth_view);
		bool load_model(std::string path, ShaderStructs::ModelData& gpu_model_data);
		bool load_terrain(const std::string& name, const TerrainDesc& desc);
		void load_gpu_data(SceneGraphTraverser& scene_graph, Ptr<UploadHeap>& upload_heap);

		void generate_hzb(sg::CommandList& command_list, sg::ShaderResourceView depth_view, SimpleLinearConstantBuffer& cbuffer);

		// Replaces the scene graph (and camera track, environment) with the contents of a scene file (models load on the next update)
		bool load_scene(const char* file_path)
		{
			if (!deserialise(root_scene_node, properties, file_path))
			{
				return false;
			}
			scene_load_count++;
			return true;
		}

		// Empties the scene graph (and camera track, environment)
		void clear_scene()
		{
			root_scene_node = SceneNode();
			properties = SceneProperties();
			scene_load_count++;
		}

		const CameraTrack& get_camera_track() const { return properties.camera_track; }

		// Automated tests (see TestRunner): applies debug / sorting / pipeline settings, resetting those not given
		void apply_test_settings(const nlohmann::json& settings);

		struct Statistics
		{
			sg::u32 total_instances;
			sg::u32 total_mesh_parts;
			sg::u32 total_meshlets;
			sg::u32 total_lights;
			sg::u32 texture_slices;
			sg::u32 terrain_tiles;		// Terrain meshlets per frame (over all terrain instances)
			sg::u32 bucket_meshlets[GDR_BUCKET_COUNT]; // Meshlets submitted to culling per render bucket

			ShaderStructs::DebugFeedbackData raw_feedback_data = {};
		};
		const Statistics& get_statistics() const { return statistics; }
		// GPU culling feedback of the last frame the GPU finished (written in debug mode only)
		ShaderStructs::DebugFeedbackData read_debug_feedback() const;
		// Incremented each time a scene file is loaded (so users of the camera track know when it changed)
		sg::u32 get_scene_load_count() const { return scene_load_count; }

		sg::SharedPtr<sg::Texture> get_hzb_texture() const { return gpu_hzb.get_texture(); }

	private:
		void update_lighting(const SceneGraphTraverser& scene_graph, Ptr<UploadHeap>& upload_heap);

		SceneNode root_scene_node;
		SceneProperties properties;
		sg::u32 scene_load_count = 0;
		Editor editor;

		sg::SharedPtr<sg::Device> device;

		GPUSort	gpu_sort;
		HZB gpu_hzb;

		ModelBuffers model_buffers;
		// Lighting, rebuilt every frame from the scene graph's light components and the scene environment
		GPUBuffer<ShaderStructs::LightData> light_data;
		GPUBuffer<ShaderStructs::EnvironmentData> environment_data;
		bool reported_light_limit = false;
		// Model paths that failed to import; skipped so a bad path does not trigger a reload every frame.
		std::set<std::string> failed_model_paths;
		struct PerInstanceRenderInfo
		{
			sg::u32 instance_offset;
			sg::u32 count;
			sg::u32 meshlet_offset;
			sg::u32 meshlets_per_instance;
			sg::u32 bucket;
		};
		std::vector<PerInstanceRenderInfo> per_instance_render_list;

		// Each render bucket owns a region of the visible meshlet list (gpu_pre_process_instances_output) sized for every
		// meshlet that could be visible, rounded up to a power of two so the region can be sorted on its own
		struct BucketRegion
		{
			sg::u32 offset = 0;
			sg::u32 size = 0;			// 0 when the bucket has no meshlets this frame
		};
		BucketRegion bucket_regions[GDR_BUCKET_COUNT];

		sg::SharedPtr<sg::IndirectArguments> mesh_shader_indirect_arguments;

		sg::UnorderedAccessView		debug_feedback_uav;
		sg::SharedPtr<sg::Buffer>	debug_feedback_buffer;
		sg::SharedPtr<sg::Buffer>	debug_readback_buffer;

		struct Pipelines
		{
			enum Type { Default, Debug, Count };
			sg::SharedPtr<sg::Pipeline> per_instance_compute_pipeline;
			sg::SharedPtr<sg::Pipeline> build_indirect_arguments_pipeline;
			sg::SharedPtr<sg::Pipeline> mesh_render_pipelines[GDR_BUCKET_COUNT]; // Pipeline state and shaders of each render bucket
		};
		Pipelines pipelines[Pipelines::Type::Count];

		struct DebugData
		{
			ShaderStructs::DebugData shader_debug_data = {};
			ShaderStructs::CameraData culling_camera_data = {};
			DirectX::BoundingFrustum culling_camera_frustum = {};
		} debug_data;

		struct RenderOptions
		{
			bool recreate_pipelines = false;
			bool render_wireframe = false;
			bool depth_enable = true;
			bool depth_write = true;
			int cull_mode = (int) sg::Rasterizer::CullMode::Back;
			int depth_comparison = (int) sg::ComparisonFunction::LessEqual;
			bool render_bucket[GDR_BUCKET_COUNT] = { true, true, true }; // Debug: skip a bucket's indirect call
		} render_options = {};

		Statistics statistics = {};
		
	};
}