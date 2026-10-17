#include "gdrScene.h"
#include <imgui.h>
#include <stack>

//assimp
#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>

/*

GPU Driven Rendering Plan
- Render many instances of the same mesh
- Ideally load a scene file of the scene setup from disk
- Coarse cull each instance on the GPU via compute shader
- Perhaps also put scene in to a BVH for faster culling
- Occlusion culling with 1 frame delay (like UE does?) - Read back on CPU for the next frame?

Extras:
- Generate a Hi-Z buffer? Then do per-meshlet occlusion culling?



Specific Rendering Process

 - Loop over the screen file and put the data in to a raw GPU buffer containing all required information
 - - Apply parent transforms and properties
 - - Ensure sphere & aabb or obb is present

 GPU Stage 1:
 - Per-object frustum & distance culling
 
 GPU Stage 2:
 - Render Scene (use occlusion data from previous frame)

 GPU Stage 2 
 - obb occlusion testing




 Buffers Needed
 - GPU Data Buffer With All Draw Data + unique id
 - GPU Occlusion Writeback Buffer

*/


/*

Instanced Mesh Rendering Specifics:

- I want a single dispatch for ALL models - including different materail types
- Meshlet data should remain the same for each loaded model


// Steps:
// Invoke an amplification shader per-instance?

// The final dispatch ID has to basically be the total number of meshlets which will be processed.
// But the number of meshlets can vary per model
// We can also have multiple instances of the same model being rendered
// The meshlet size can vary per-model too.

// Options for the differing meshlet sizes
- Have a maximum number of meshlets allocated per mesh (wasteful)
- Each dispatch ID has a meshlet and index associated with it?


// So final process would be:
	- We dispatch for the total number of meshlets.
	- Each meshlet thread has an instance id and 

	// instance 0 - model 0 - meshlet count 4
	// instance 1 - model 0 - meshlet count 4


// Compute shader called per-instance
	- Each thread has the following data:
		- instance id -> model id (contains meshlet count)

	- compute shader does coarse culling at entire model level

	- compute shader builds up a new intermediate meshlet list
	- each meshlet entry contains:
		- instance id -> model id
		- meshlet index

	- We then have an indirect dispatch mesh call, iterating over everything in our intermediate list


	// Buffer Layout
	- Per Instance Buffer - Contains All Instances - Instances of the same type are sequential
*/

namespace gdr
{
	template <typename T_NODE_FUNCTION>
	static void scene_node_traverse(SceneNode& scene_node, T_NODE_FUNCTION callback)
	{
		for (SceneNode& child_node : scene_node.children)
		{
			scene_node_traverse(child_node, callback);
		}

		callback(scene_node);
	}

	Scene::Scene(sg::SharedPtr<sg::Device>& _device) : device(_device)
	{

	}

	void Scene::update(Ptr<UploadHeap>& upload_heap)
	{
		editor.imgui_scene_overlay(*this);

		SceneGraphTraverser scene_graph_traverser;
		scene_graph_traverser.process_scene_graph(root_scene_node);

		load_gpu_data(scene_graph_traverser, upload_heap);

	}

	void Scene::render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw)
	{

	}

	static std::vector<Vertex> load_vertex_buffer(const aiMesh* assimp_mesh)
	{
		using namespace DirectX;

		std::vector<Vertex> vertices;

		for (uint32_t vert_idx = 0; vert_idx < assimp_mesh->mNumVertices; vert_idx++)
		{
			Vertex v;

			v.Position = XMFLOAT3(&assimp_mesh->mVertices[vert_idx].x);
			v.Color = /*use_color ? XMFLOAT3(assimp_mesh->mColors[0][vert_idx].r, assimp_mesh->mColors[0][vert_idx].g, assimp_mesh->mColors[0][vert_idx].b) :*/ XMFLOAT3(1.0f, 1.0f, 1.0f);
			v.UV = assimp_mesh->mTextureCoords[0] ? XMFLOAT2(&assimp_mesh->mTextureCoords[0][vert_idx].x) : XMFLOAT2(0, 0);
			v.Normal = XMFLOAT3(&assimp_mesh->mNormals[vert_idx].x);
			v.Tangent = assimp_mesh->mTangents ? XMFLOAT3(&assimp_mesh->mTangents[vert_idx].x) : XMFLOAT3(0, 0, 0);
			vertices.push_back(v);
		}

		return vertices;
	}

	static std::vector<uint32_t> load_index_buffer(const aiMesh* assimp_mesh)
	{
		std::vector<uint32_t> indices;
		for (uint32_t face_idx = 0; face_idx < assimp_mesh->mNumFaces; face_idx++)
		{
			// For some reason we're getting a face idx through where mNumIndices is 2! But the documentation lies! 
			// "Together with the #aiProcess_Triangulate flag you can then be sure that *#aiFace::mNumIndices is always 3."

			//seAssert(aMesh->mFaces[face_idx].mNumIndices == 3, "Expected 3 indicies per face...");					
			if (assimp_mesh->mFaces[face_idx].mNumIndices == 3)
			{
				indices.push_back(assimp_mesh->mFaces[face_idx].mIndices[0]);
				indices.push_back(assimp_mesh->mFaces[face_idx].mIndices[1]);
				indices.push_back(assimp_mesh->mFaces[face_idx].mIndices[2]);
			}
		}

		return indices;
	}

	void Scene::load_model(std::string path)
	{
		Assimp::Importer ai_importer;
		const int flags = aiProcess_Triangulate
			| aiProcess_PreTransformVertices
			| aiProcess_CalcTangentSpace
			| aiProcess_GenSmoothNormals
			| aiProcess_JoinIdenticalVertices
			| aiProcess_SplitLargeMeshes
			| aiProcess_ConvertToLeftHanded;

		const aiScene* aScene = ai_importer.ReadFile(path.c_str(), flags); //By passing file path, allows assimp to auto load material files.
		seAssert(aScene != nullptr, "Failed To Load Assimp Scene %s\n%s", path.c_str(), ai_importer.GetErrorString());
		if (!aScene)
			return;

		ModelBuffers::PerModelData& new_model = model_buffers.models.emplace_back();
		new_model.name = path;

		new_model.mesh_parts.resize(aScene->mNumMeshes);

		for (size_t mesh_part_idx = 0; mesh_part_idx < new_model.mesh_parts.size(); mesh_part_idx++)
		{
			ModelBuffers::PerModelData::MeshParts& mesh_part = new_model.mesh_parts[mesh_part_idx];
			mesh_part = {};

			mesh_part.vertex_data_offset = model_buffers.vertex_data.cpu_data.size();
			mesh_part.meshlet_data_offset = model_buffers.meshlet_data.cpu_data.size();
			mesh_part.meshlet_vertex_index_offset = model_buffers.meshlet_vertex_index.cpu_data.size();
			mesh_part.meshlet_primitive_index_offset = model_buffers.meshlet_primitive_index.cpu_data.size();

			aiMesh* assimp_mesh = aScene->mMeshes[mesh_part_idx];
			mesh_part.material_index = assimp_mesh->mMaterialIndex;

			// Load normal vertex and index buffer data (only used to generate meshlet data from
			std::vector<Vertex> vertices = load_vertex_buffer(assimp_mesh);
			std::vector<uint32_t> indices = load_index_buffer(assimp_mesh);

			// Create meshlets
			{
				const size_t max_vertices = 64;
				const size_t max_triangles = 124;
				const float cone_weight = 0.0f;
				const size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles);

				std::vector<meshopt_Meshlet> tmp_meshlets;
				std::vector<unsigned int>	 tmp_meshlet_vertices;
				std::vector<unsigned char>	 tmp_meshlet_triangles;

				tmp_meshlets.resize(max_meshlets);
				tmp_meshlet_vertices.resize(max_meshlets * max_vertices);
				tmp_meshlet_triangles.resize(max_meshlets * max_triangles * 3);

				const size_t meshlet_count = mesh_part.meshlet_count = meshopt_buildMeshlets(tmp_meshlets.data(), tmp_meshlet_vertices.data(), tmp_meshlet_triangles.data(), indices.data(),
					indices.size(), (float*)&vertices[0], vertices.size(), sizeof(Vertex), max_vertices, max_triangles, cone_weight);

				// Trim excess of vectors
				const meshopt_Meshlet& last = tmp_meshlets[meshlet_count - 1];
				tmp_meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
				tmp_meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
				tmp_meshlets.resize(meshlet_count);

				// TODO: Add meshopt_optimizeMeshlet

				// Now place our data for how it will be used on the GPU.				

				// Meshlet Data
				for (meshopt_Meshlet& meshlet : tmp_meshlets)
				{
					ShaderStructs::MeshletData& gpu_meshlet = model_buffers.meshlet_data.cpu_data.emplace_back();
					
					gpu_meshlet.vertex_buffer_offset = mesh_part.vertex_data_offset;
					gpu_meshlet.vertex_offset		 = mesh_part.meshlet_vertex_index_offset + meshlet.vertex_offset;
					gpu_meshlet.triangle_offset		 = mesh_part.meshlet_primitive_index_offset + meshlet.triangle_offset;
					gpu_meshlet.vertex_count		 = meshlet.vertex_count;
					gpu_meshlet.triangle_count		 = meshlet.triangle_count;					
				}

				// Vertex Data
				for (const Vertex& v : vertices)
				{
					model_buffers.vertex_data.cpu_data.push_back(v);
				}

				// Vertex Index
				for (sg::u32 vid : tmp_meshlet_vertices)
				{
					model_buffers.meshlet_vertex_index.cpu_data.push_back(vid);
				}

				// Index Data
				for (meshopt_Meshlet& meshlet : tmp_meshlets)
				{
					for (size_t tri_idx = 0; tri_idx < meshlet.triangle_count; tri_idx++)
					{
						const sg::u32 tri_offset = meshlet.triangle_offset + (tri_idx * 3);
						seAssert((tri_offset + 2) < tmp_meshlet_triangles.size(), "Reading too many tris");
						sg::u32 tri = 0;
						tri |= static_cast<sg::u32>(tmp_meshlet_triangles[tri_offset + 0]) << 0u;
						tri |= static_cast<sg::u32>(tmp_meshlet_triangles[tri_offset + 1]) << 8u;
						tri |= static_cast<sg::u32>(tmp_meshlet_triangles[tri_offset + 2]) << 16u;

						model_buffers.meshlet_primitive_index.cpu_data.push_back(tri);
					}
				}
			}	
		}

		ai_importer.FreeScene();
	}

	void Scene::load_gpu_data(SceneGraphTraverser& scene_graph, Ptr<UploadHeap>& upload_heap)
	{
		bool recreate_gpu_buffers = false;

		if (scene_graph.unique_model_paths.size() != model_buffers.models.size())
		{
			recreate_gpu_buffers = true;
		}
		else 
		{
			for (auto& model : scene_graph.unique_model_paths)
			{
				for (auto& loaded_models : model_buffers.models)
				{
					if (model != loaded_models.name)
					{
						recreate_gpu_buffers = true;
						break;
					}
				}
			}
		}

		if (!recreate_gpu_buffers)
		{
			return;
		}

		model_buffers = {};

		for (auto& model : scene_graph.unique_model_paths)
		{
			load_model(model);
		}

		// Upload all model data to GPU
	}

	// Scene Graph Traverser
	void SceneGraphTraverser::process_scene_graph(SceneNode& root_node)
	{
		auto process_node = [this](const SceneNode& scene_node)
			{
				if (scene_node.model_name != kINVALID_MODEL_NAME)
				{
					unique_model_paths.insert(scene_node.model_name);
				}

				using namespace DirectX;

				PerModelData& node_data = nodes.emplace_back();
				node_data.model_matrix = XMMatrixIdentity();
				node_data.scene_node = &scene_node;

				// Construct the final matrix for this node - Walk up the parent nodes
				const SceneNode* current_node = &scene_node;
				while (current_node != nullptr)
				{
					XMMATRIX node_matrix = XMMatrixScaling(scene_node.scale.x, scene_node.scale.y, scene_node.scale.z);

					const XMMATRIX rotation_matrix = XMMatrixRotationRollPitchYaw(XMConvertToRadians(scene_node.rotation.x), XMConvertToRadians(scene_node.rotation.y), XMConvertToRadians(scene_node.rotation.z));
					node_matrix = XMMatrixMultiply(node_matrix, rotation_matrix);

					const XMMATRIX translation_matrix = XMMatrixTranslation(scene_node.position.x, scene_node.position.y, scene_node.position.z);
					node_matrix = XMMatrixMultiply(node_matrix, translation_matrix);

					// Now multiply this nodes matrix in to child
					node_data.model_matrix = XMMatrixMultiply(node_data.model_matrix, node_matrix);

					// Apply parents matrix next
					current_node = current_node->parent;
				}
			};

		scene_node_traverse(root_node, process_node);
	}
}