#include "gdrScene.h"
#include <imgui.h>
#include <stack>

//assimp
#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>
#include "sgUtils.h"

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
		create_pipelines();
	}

	void Scene::create_pipelines()
	{
		using namespace sg;

		BindingDesc global_binding_desc;
		global_binding_desc.uav_binding_count = 3;
		global_binding_desc.srv_binding_count = 5;
		global_binding_desc.cbv_binding_count = 2;

		const char* shaders[Pipelines::Type::Count][4] =
		{
			{
				"ShaderBin_Debug\\GDR_PerInstanceProcessingMain.PC_DXC",
				"ShaderBin_Debug\\GDR_BuildIndirectArguments.PC_DXC",
				"ShaderBin_Debug\\GDR_MeshShader.PC_DXC",
				"ShaderBin_Debug\\GDR_PixelShader.PC_DXC"
			},
			{
				"ShaderBin_Debug\\GDR_PerInstanceProcessingMain_DEBUG.PC_DXC",
				"ShaderBin_Debug\\GDR_BuildIndirectArguments_DEBUG.PC_DXC",
				"ShaderBin_Debug\\GDR_MeshShader_DEBUG.PC_DXC",
				"ShaderBin_Debug\\GDR_PixelShader_DEBUG.PC_DXC"
			}
		};

		for (size_t i = 0; i < Pipelines::Type::Count; i++)
		{
			Pipelines& pipeline = pipelines[i];

			if (i == Pipelines::Type::Debug)
			{
				global_binding_desc.cbv_binding_count = 3;
			}

			// Compute
			{
				PipelineDesc::Compute desc;
				desc.compute_shader = sg::create_compute_shader(*device, shaders[i][0]);
				pipeline.per_instance_compute_pipeline = device->create_pipeline(desc, global_binding_desc);
				seAssert(pipeline.per_instance_compute_pipeline != nullptr, "Failed to create pipeline");

				desc.compute_shader = sg::create_compute_shader(*device, shaders[i][1]);
				pipeline.build_indirect_arguments_pipeline = device->create_pipeline(desc, global_binding_desc);
				seAssert(pipeline.build_indirect_arguments_pipeline != nullptr, "Failed to create pipeline");

				mesh_shader_indirect_arguments = device->create_indirect_arguments();
			}

			// Mesh Shader
			{
				PipelineDesc::Mesh desc;

				desc.mesh_shader = sg::create_mesh_shader(*device, shaders[i][2]);
				desc.pixel_shader = sg::create_pixel_shader(*device, shaders[i][3]);
				desc.render_target_count = 1;
				desc.render_target_format_list[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // TODO - UNDO HARDCODING
				desc.depth_stencil_format = DXGI_FORMAT_D32_FLOAT; // TODO - UNDO HARDCODING

				desc.depth_stencil_desc.depth_enable = render_options.depth_enable;
				desc.depth_stencil_desc.depth_write = render_options.depth_write;

				const ComparisonFunction dcf = render_options.depth_comparison != 0 ? (ComparisonFunction)render_options.depth_comparison : ComparisonFunction::Never; // Not actually supported to pass None..
				desc.depth_stencil_desc.comparison_function = dcf;
				desc.rasterizer_desc.cull_mode = (Rasterizer::CullMode) render_options.cull_mode;
				desc.rasterizer_desc.fill_mode = render_options.render_wireframe ? Rasterizer::FillMode::Wireframe : Rasterizer::FillMode::Solid;

				pipeline.mesh_render_pipeline = device->create_pipeline(desc, global_binding_desc);
				seAssert(pipeline.mesh_render_pipeline != nullptr, "Failed to create pipeline");
			}
		}
	}

	void Scene::imgui()
	{
		if (!ImGui::Begin("GDR Scene"))
		{
			return;
		}

		if (ImGui::CollapsingHeader("Debug Options", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Debug Mode", (bool*) &debug_data.debug_mode);
			ImGui::BeginDisabled(debug_data.debug_mode == 0);
			ImGui::Indent();
			ImGui::Checkbox("Cull All Instances", (bool*)&debug_data.cull_all_instances);
			ImGui::Unindent();
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Rendering Options", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Pipeline");
			ImGui::Indent();
			render_options.recreate_pipelines |= ImGui::Checkbox("Wireframe", &render_options.render_wireframe);
			render_options.recreate_pipelines |= ImGui::Checkbox("Depth Enable", &render_options.depth_enable);
			render_options.recreate_pipelines |= ImGui::Checkbox("Depth Write", &render_options.depth_write);
			render_options.recreate_pipelines |= ImGui::Combo("Cull Mode", &render_options.cull_mode, "None\0Front\0Back\0", 3);
			render_options.recreate_pipelines |= ImGui::Combo("Depth Comparison", &render_options.depth_comparison, "None\0Never\0Less\0Equal\0LessEqual\0Greater\0NotEqual\0GreaterEqual\0Always", 9);
			ImGui::Unindent();
		}

		ImGui::End();
	}

	void Scene::update(Ptr<UploadHeap>& upload_heap)
	{
		imgui();

		editor.imgui_scene_overlay(*this);

		if (render_options.recreate_pipelines)
		{
			create_pipelines();
			render_options.recreate_pipelines = false;
		}

		SceneGraphTraverser scene_graph_traverser;
		scene_graph_traverser.process_scene_graph(root_scene_node);

		load_gpu_data(scene_graph_traverser, upload_heap);

		// Construct and upload per-instance data
		model_buffers.instance_data.cpu_data = {};
		per_instance_render_list = {};

		// Now we iterate over all loaded models and each instance within that model
		// We then add each instance we want to render to a list to send to the GPU	
		// Meshes with mutiple parts are sent as if they were separate instances	
		const auto& model_lists = scene_graph_traverser.model_instance_map;
		sg::u32 total_meshlets = 0;
		if (model_lists.size())
		{
			sg::u32 model_index = 0;
			for (const auto& [model_name, model_list] : model_lists)
			{
				const ModelBuffers::ModelData& per_model_data = model_buffers.find_model(model_name);

				// TODO: Not ideal to treat each mesh part as its own instances, wastes space and data
				for (const auto& mesh_part : per_model_data.mesh_parts)
				{
					PerInstanceRenderInfo& instance_render_info = per_instance_render_list.emplace_back();
					instance_render_info.instance_offset = model_buffers.instance_data.cpu_data.size();
					instance_render_info.count = model_list.size();
					instance_render_info.meshlet_offset = mesh_part.meshlet_data_offset;
					instance_render_info.meshlets_per_instance = mesh_part.meshlet_count;

					for (const auto& instance : model_list)
					{
						ShaderStructs::InstanceData& gpu_instance = model_buffers.instance_data.cpu_data.emplace_back();
						gpu_instance.model_matrix = instance.model_matrix;
						gpu_instance.colour = DirectX::XMFLOAT4A(instance.colour.x, instance.colour.y, instance.colour.z, 1.0f);
						gpu_instance.model_index = model_index;

						total_meshlets += mesh_part.meshlet_count;
					}
				}
				model_index++;
			}
			model_buffers.instance_data.create_and_upload_to_gpu(device, upload_heap);
			model_buffers.gpu_pre_process_instances_output.create(device, total_meshlets); // Output buffer allocated matches input
		}

	}

	void Scene::render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw)
	{
		using namespace sg;

		// Stage 1: Compute dispatch for each instance type for the per-instance-pre-pass
		// Stage TODO: Sorting Step
		// Stage 2: Execute Indirect DispatchMesh


		// Nothing in the scene to render
		if (per_instance_render_list.size() == 0)
			return;

		command_list.clear_buffer_uint(model_buffers.gpu_pre_process_instances_output.uav, model_buffers.gpu_pre_process_instances_output.srv, 0);
		command_list.clear_buffer_uint(model_buffers.atomic_counter.uav, model_buffers.atomic_counter.srv, 0);
		command_list.clear_buffer_uint(model_buffers.indirect_arguments.uav, model_buffers.indirect_arguments.srv, 0);

		Binding b;
		b.uav_binding_count = 3;
		b.srv_binding_count = 5;
		b.cbv_binding_count = 2;

		if (debug_data.debug_mode != 0)
		{
			b.cbv_binding_count = 3;
			b.set_cbv(cbuffer.AllocateAndWrite(debug_data), 2);
		}
		
		b.set_uav(model_buffers.gpu_pre_process_instances_output.uav, 0);
		b.set_uav(model_buffers.atomic_counter.uav, 1);
		b.set_uav(model_buffers.indirect_arguments.uav, 2);

		b.set_srv(model_buffers.vertex_data.srv, 0);
		b.set_srv(model_buffers.meshlet_data.srv, 1);
		b.set_srv(model_buffers.meshlet_vertex_index.srv, 2);
		b.set_srv(model_buffers.meshlet_primitive_index.srv, 3);
		b.set_srv(model_buffers.instance_data.srv, 4);
		//b.set_srv(model_buffers.model_data.srv, 5);
		
		b.set_cbv(cbv_camera, 0);

		command_list.set_pipeline(pipelines[debug_data.debug_mode].per_instance_compute_pipeline.get());

		for (const PerInstanceRenderInfo& instance_section : per_instance_render_list)
		{
			ShaderStructs::PerInstanceProcessingData pre_process_cbuffer = {};
			pre_process_cbuffer.instance_offset = instance_section.instance_offset;
			pre_process_cbuffer.meshlets_per_instance = instance_section.meshlets_per_instance;
			pre_process_cbuffer.meshlet_offset = instance_section.meshlet_offset;
			pre_process_cbuffer.meshlets_to_process_count = instance_section.count * instance_section.meshlets_per_instance;
			
			ConstantBufferView cbv = cbuffer.AllocateAndWrite(pre_process_cbuffer);
			b.set_cbv(cbv, 1);

			command_list.bind(b, PipelineType::Compute);
			command_list.dispatch(sg::AlignUp<uint32_t>(pre_process_cbuffer.meshlets_to_process_count, 32u) / 32u, 1, 1);
		}

		// Stage 1.5 - Build indirect arguments
		command_list.set_pipeline(pipelines[debug_data.debug_mode].build_indirect_arguments_pipeline.get());
		command_list.dispatch(1, 1, 1);

		// Stage 2
		// So at this point we now have a list of final meshlets to be rendered in model_buffers.gpu_pre_process_instances_output
		
		command_list.set_pipeline(pipelines[debug_data.debug_mode].mesh_render_pipeline.get());
		command_list.bind(b, PipelineType::Geometry);

		mesh_shader_indirect_arguments->set_indirect_buffer(model_buffers.indirect_arguments.buffer);
		command_list.execute_indirect(*mesh_shader_indirect_arguments);
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

	void Scene::load_model(std::string path, ShaderStructs::ModelData& gpu_model_data)
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

		ModelBuffers::ModelData& new_model = model_buffers.models.emplace_back();
		new_model.name = path;

		new_model.mesh_parts.resize(aScene->mNumMeshes);

		for (size_t mesh_part_idx = 0; mesh_part_idx < new_model.mesh_parts.size(); mesh_part_idx++)
		{
			ModelBuffers::ModelData::MeshPart& mesh_part = new_model.mesh_parts[mesh_part_idx];
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
				tmp_meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3)); // Align total to be multiple of 4
				tmp_meshlets.resize(meshlet_count);

				// TODO: Add meshopt_optimizeMeshlet

				// Now place our data for how it will be used on the GPU.				

				// Meshlet Data
				for (meshopt_Meshlet& meshlet : tmp_meshlets)
				{
					ShaderStructs::MeshletData& gpu_meshlet = model_buffers.meshlet_data.cpu_data.emplace_back();
					
					gpu_meshlet.vertex_buffer_offset = mesh_part.vertex_data_offset;
					gpu_meshlet.vertex_offset		 = mesh_part.meshlet_vertex_index_offset + meshlet.vertex_offset;
					gpu_meshlet.triangle_offset		 = model_buffers.meshlet_primitive_index.cpu_data.size(); // Offset by the current primitive index offset.
					gpu_meshlet.vertex_count		 = meshlet.vertex_count;
					gpu_meshlet.triangle_count		 = meshlet.triangle_count;

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
			}	
		}

		ai_importer.FreeScene();
	}

	void Scene::load_gpu_data(SceneGraphTraverser& scene_graph, Ptr<UploadHeap>& upload_heap)
	{
		bool recreate_gpu_buffers = false;

		if (scene_graph.model_instance_map.size() != model_buffers.models.size())
		{
			recreate_gpu_buffers = true;
		}
		else 
		{
			for (auto& [model_string, model_list] : scene_graph.model_instance_map)
			{
				bool found_model = false;
				for (auto& loaded_models : model_buffers.models)
				{
					if (model_string == loaded_models.name)
					{
						found_model = true;
						break;
					}
				}
				if (!found_model)
				{
					recreate_gpu_buffers = true;
					break;
				}
			}
		}

		if (!recreate_gpu_buffers)
		{
			return;
		}

		model_buffers = {};
		
		for (auto& [model_string, model_list] : scene_graph.model_instance_map)
		{
			ShaderStructs::ModelData& model_gpu_data = model_buffers.model_data.cpu_data.emplace_back();
			load_model(model_string, model_gpu_data);
		}

		// Upload all model data to GPU
		model_buffers.create_and_upload_to_gpu(device, upload_heap);
	}

	// Scene Graph Traverser
	void SceneGraphTraverser::process_scene_graph(SceneNode& root_node)
	{
		auto process_node = [this](const SceneNode& scene_node)
			{
				if (scene_node.model_name == kINVALID_MODEL_NAME)
				{
					return;
				}

				auto& instance_list = model_instance_map[scene_node.model_name];

				using namespace DirectX;

				InstanceData& node_data = instance_list.emplace_back();
				node_data.model_matrix = XMMatrixIdentity();
				node_data.colour = { 1.0f, 1.0f, 1.0f };
				node_data.scene_node = &scene_node;

				// Construct the final matrix for this node - Walk up the parent nodes
				const SceneNode* current_node = &scene_node;
				while (current_node != nullptr)
				{
					XMMATRIX node_matrix = XMMatrixScaling(current_node->scale.x, current_node->scale.y, current_node->scale.z);

					const XMMATRIX rotation_matrix = XMMatrixRotationRollPitchYaw(XMConvertToRadians(current_node->rotation.x), XMConvertToRadians(current_node->rotation.y), XMConvertToRadians(current_node->rotation.z));
					node_matrix = XMMatrixMultiply(node_matrix, rotation_matrix);

					const XMMATRIX translation_matrix = XMMatrixTranslation(current_node->position.x, current_node->position.y, current_node->position.z);
					node_matrix = XMMatrixMultiply(node_matrix, translation_matrix);

					// Now multiply this nodes matrix in to child
					node_data.model_matrix = XMMatrixMultiply(node_data.model_matrix, node_matrix);

					node_data.colour.x *= current_node->colour.x;
					node_data.colour.y *= current_node->colour.y;
					node_data.colour.z *= current_node->colour.z;

					// Apply parents matrix next
					current_node = current_node->parent;
				}
			};

		scene_node_traverse(root_node, process_node);
	}


	const ModelBuffers::ModelData& ModelBuffers::find_model(std::string_view name)
	{
		for (const auto& model : models)
		{
			if (model.name == name)
			{
				return model;
			}
		}
		seAssert(false, "Failed to find model!");
		return models[0];
	}

	void ModelBuffers::create_and_upload_to_gpu(sg::SharedPtr<sg::Device>& device, sg::Ptr<sg::UploadHeap>& upload_heap)
	{
		// NODE: We do this separately. 
		meshlet_data.create_and_upload_to_gpu(device, upload_heap);
		material_data.create_and_upload_to_gpu(device, upload_heap);
		vertex_data.create_and_upload_to_gpu(device, upload_heap);
		meshlet_vertex_index.create_and_upload_to_gpu(device, upload_heap);
		meshlet_primitive_index.create_and_upload_to_gpu(device, upload_heap);
		//model_data.create_and_upload_to_gpu(device, upload_heap);

		atomic_counter.create(device, 1);
		indirect_arguments.create(device, 1);
	}
}