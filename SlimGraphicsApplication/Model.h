#pragma once
#include <sgTypes.h>
#include <sgPlatformInclude.h>
#include <seEngineBasicFileIO.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include "sgUploadHeap.h"
#include <meshoptimizer.h>
#include <DirectXMesh.h>

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

	struct InitData
	{
		std::string file_path;

		//bool scalemodel1to1 = false; // Problems with this so removed for now.

		// Mesh Optimizer settings
		bool meshopt_overdraw = false;
		bool meshopt_vertex_fetch = false;
		bool meshopt_vertex_quantization = false;
		bool meshopt_simplification = false;
		float meshopt_simplification_threshold = 0.2f;
		float meshopt_simplification_target_error = 0.01f;

		bool meshopt_meshlets = true;
		size_t meshopt_meshlets_max_vertices = 64;
		size_t meshopt_meshlets_max_triangles = 126;
		float meshopt_meshlets_cone_weight = 0.0f;
		bool meshopt_meshlet_optimize = false;

		enum class VertexCachOptimisation : int
		{
			Default,
			MeshOpt,
			DXMesh,
			DXMeshLRU
		};
		VertexCachOptimisation vertex_cache_opt_mode = VertexCachOptimisation::Default;
		int dxmesh_vertex_cache_size = DirectX::OPTFACES::OPTFACES_V_DEFAULT;
		int dxmesh_restart = DirectX::OPTFACES::OPTFACES_R_DEFAULT;
		int dxmesh_lru_size = DirectX::OPTFACES::OPTFACES_LRU_DEFAULT;

	};

public:
	Model(sg::Device* device, sg::UploadHeap* upload_heap, const InitData& _init_data);

	struct MeshShadingData
	{
		std::vector<meshopt_Meshlet> meshlets;
		std::vector<meshopt_Bounds> meshlet_bounds;
		std::vector<unsigned int> meshlet_vertices;
		std::vector<unsigned char> meshlet_triangles;
		std::vector<sg::u32> meshlet_triangles_gpu; // 1 tri per u32

		//sg::Ptr<sg::Buffer>	gpu_vertices;
		sg::SharedPtr<sg::Buffer>	gpu_meshlets;
		sg::UnorderedAccessView		gpu_meshlets_view;
		sg::ShaderResourceView		gpu_meshlets_view_srv;

		sg::SharedPtr<sg::Buffer>	gpu_unique_vertex_indices;
		sg::UnorderedAccessView		gpu_unique_vertex_indices_view;
		sg::ShaderResourceView		gpu_unique_vertex_indices_view_srv;

		sg::SharedPtr<sg::Buffer>	gpu_primitive_indices;
		sg::UnorderedAccessView		gpu_primitive_indices_view;
		sg::ShaderResourceView		gpu_primitive_indices_view_srv;

		sg::SharedPtr<sg::Buffer>	gpu_culldata;
		sg::UnorderedAccessView		gpu_culldata_view;
		sg::ShaderResourceView		gpu_culldata_view_srv;
	};

	struct MeshPart
	{
		sg::u32 material_index = 0;
		sg::u32 vb_offset = 0; // Offset in stride count (not bytes)
		sg::u32 ib_offset = 0; // Offset in element count (not bytes)
		sg::u32 draw_count = 0;
		sg::u32 vertex_count = 0;
		DirectX::XMFLOAT3 max_extent = {};
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		MeshShadingData mesh_shader_data;

		DirectX::XMFLOAT3 bounding_box_min = { FLT_MAX, FLT_MAX, FLT_MAX };
		DirectX::XMFLOAT3 bounding_box_max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		DirectX::BoundingBox aabb;


		// ACMR (Average Cache Miss Ratio)
		// Best = 0.5, Worst = 3.0
		// 
		// ACMR is Average Cache Miss Ratio, which is used to measure the effectiveness of a vertex reordering scheme to see how it performs. 
		// That is, the GPU has a certain number of transformed vertices it keeps in its post-transformed vertex cache. 
		// This cache will be more or less effective, depending on the order you feed triangles into the GPU. 
		// ACMR is the sum of the number of times each vertex must be transformed and loaded into the cache, divided by the number of triangles in the mesh. 
		// Under perfect conditions it is 0.5, since the most a cached vertex can be shared on average in a (non-bizarre) mesh is 6 times and each triangle has 3 vertices.
		float vertex_cache_miss_acmr = 0.0f;

		// ATVR (average transform to vertex ratio)
		// Best = 1.0, Worst = 6.0
		// 
		// ATVR is a better measure of cache performance, as it provides a number that can be judged by itself: 1.0 is always the optimum, so if your caching scheme is giving 1.05 ATVR, you’re doing pretty good. 
		// The worst ATVR can get is 6.0 (or just shy of 6.0).
		float vertex_cache_miss_atvr = 0.0f;
	};

	struct Material
	{
		std::string material_name;
		sg::SharedPtr<sg::Texture> tex_diffuse;
		sg::ShaderResourceView srv_diffuse;
		sg::SharedPtr<sg::Texture> tex_specular;
		sg::ShaderResourceView srv_specular;
		sg::SharedPtr<sg::Texture> tex_normal;
		sg::ShaderResourceView srv_normal;
	};

	struct DebugModels
	{
		struct DrawInfo { sg::u32 draw_count = 0, vertex_offset = 0, index_offset = 0; };
		std::vector<DrawInfo> cubes;
		std::vector<DrawInfo> spheres_simple;
		std::vector<DrawInfo> spheres_complex;
		sg::SharedPtr<sg::Buffer> vb;
		sg::SharedPtr<sg::Buffer> ib;
	};

	void SetPipeline(sg::Ptr<sg::Pipeline> new_pipeline);
	void Render(sg::CommandList* command_list, sg::ConstantBufferView& cbv_camera, sg::ConstantBufferView& cbv_model);
	sg::VertexBufferView& GetVertexBufferView() { return vbv; }
	sg::ShaderResourceView& GetVertexBufferSRV() { return vb_srv; }
	sg::IndexBufferView& GetIndexBufferView() { return ibv; }
	std::vector<MeshPart>& GetMeshParts() { return mesh_parts; }
	Material& GetMaterial(sg::u32 material_index) { return materials[material_index]; }

private:
	InitData init_data;
	sg::Ptr<sg::Pipeline> pipeline;
	sg::SharedPtr<sg::Buffer> vb;
	sg::SharedPtr<sg::Buffer> ib;
	sg::VertexBufferView vbv;
	sg::ShaderResourceView vb_srv;
	sg::IndexBufferView ibv;
	std::vector<MeshPart> mesh_parts;
	std::vector<Material> materials;

	sg::SharedPtr<sg::Texture> default_texture;
	sg::ShaderResourceView default_texture_srv;
public:
	DirectX::XMFLOAT3 max_extent = {};
	DirectX::XMFLOAT3 bounding_box_min = { FLT_MAX, FLT_MAX, FLT_MAX };
	DirectX::XMFLOAT3 bounding_box_max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

	DebugModels debug_models;
};

