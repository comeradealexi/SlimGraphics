#include "gdrScene.h"
#include <imgui.h>
#include "ImGuiLayout.h"
#include <stack>

//assimp
#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>
#include "sgUtils.h"
#include <Utils.h>
#include <meshoptimizer.h>
#include <GeometricPrimitive.h>
#include <DirectXMesh.h>
#include "ShaderSharedStructures.h"
#include <lodepng.h>
#include "stb_image.h"
#include "sgUtils.h"
#include <utility>
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
	// Shader resource registers shared by every GDR pass (see gdrShaderCompute.hlsl)
	enum SRVRegister : sg::u32
	{
		SRV_VERTICES, SRV_MESHLETS, SRV_MESHLET_VERTEX_INDICES, SRV_MESHLET_PRIMITIVES, SRV_INSTANCES,
		SRV_MATERIALS, SRV_LIGHTS, SRV_ENVIRONMENT, SRV_TERRAINS, SRV_TERRAIN_HEIGHTS, SRV_MATERIAL_TEXTURES,
		SRV_COUNT
	};

	static DirectX::XMFLOAT3 Max(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
	{
		DirectX::XMFLOAT3 o;
		o.x = std::max<float>(a.x, b.x);
		o.y = std::max<float>(a.y, b.y);
		o.z = std::max<float>(a.z, b.z);
		return o;
	}

	static DirectX::XMFLOAT3 Min(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
	{
		DirectX::XMFLOAT3 o;
		o.x = std::min<float>(a.x, b.x);
		o.y = std::min<float>(a.y, b.y);
		o.z = std::min<float>(a.z, b.z);
		return o;
	}

	template <typename T_NODE_FUNCTION>
	static void scene_node_traverse(SceneNode& scene_node, T_NODE_FUNCTION callback)
	{
		for (SceneNode& child_node : scene_node.children)
		{
			scene_node_traverse(child_node, callback);
		}

		callback(scene_node);
	}

	Scene::Scene(sg::SharedPtr<sg::Device>& _device, sg::SharedPtr<sg::Texture> depth_texture) : device(_device), gpu_sort(device), gpu_hzb(depth_texture, device)
	{
		create_pipelines();

		// Created once: create_pipelines() can be called again at runtime (pipeline option changes)
		mesh_shader_indirect_arguments = device->create_indirect_arguments();

		// Some defaults for debug
		debug_data.shader_debug_data.use_frustum_culling = true;
		debug_data.shader_debug_data.use_cone_culling = true;
		debug_data.shader_debug_data.sort_mode = 1; // Sort front to back

		auto feedback_memory = device->allocate_memory(sg::MemoryType::GPUOptimal, sg::MemorySubType::Buffer, sizeof(gdr::ShaderStructs::DebugFeedbackData));
		debug_feedback_buffer = device->create_buffer(feedback_memory, sizeof(gdr::ShaderStructs::DebugFeedbackData), sg::BufferType::GeneralDataBuffer, true);
		debug_feedback_uav = device->create_unordered_access_view(debug_feedback_buffer, sizeof(gdr::ShaderStructs::DebugFeedbackData), 1);

		auto readback_memory = device->allocate_memory(sg::MemoryType::Readback, sg::MemorySubType::Buffer, sizeof(gdr::ShaderStructs::DebugFeedbackData));
		debug_readback_buffer = device->create_buffer(readback_memory, sizeof(gdr::ShaderStructs::DebugFeedbackData), sg::BufferType::GeneralDataBuffer, false);
	}

	void Scene::create_pipelines()
	{
		using namespace sg;

		BindingDesc global_binding_desc;
		global_binding_desc.uav_binding_count = 3;
		global_binding_desc.srv_binding_count = SRV_COUNT;
		global_binding_desc.cbv_binding_count = 2;
		global_binding_desc.sampler_binding_count = 4; // s0: material textures, s3: trilinear (alpha test)

		const char* compute_shaders[2] = { "GDR_PerInstanceProcessingMain", "GDR_BuildIndirectArguments" };
		// Mesh and pixel shader of each render bucket (GDR_BUCKET_*), specialised so they never branch on meshlet type
		const char* bucket_shaders[GDR_BUCKET_COUNT][2] =
		{
			{ "GDR_MeshShader", "GDR_PixelShader" },				// GDR_BUCKET_OPAQUE
			{ "GDR_MeshShader", "GDR_PixelShaderFoliage" },			// GDR_BUCKET_FOLIAGE
			{ "GDR_MeshShaderTerrain", "GDR_PixelShaderTerrain" },	// GDR_BUCKET_TERRAIN
		};
		auto shader_name = [](const char* name, size_t type) { return std::string(name) + (type == Pipelines::Type::Debug ? "_DEBUG" : ""); };

		for (size_t i = 0; i < Pipelines::Type::Count; i++)
		{
			Pipelines& pipeline = pipelines[i];

			if (i == Pipelines::Type::Debug)
			{
				global_binding_desc.uav_binding_count = 4;
				global_binding_desc.cbv_binding_count = 4;
			}

			// Compute
			{
				PipelineDesc::Compute desc;
				desc.compute_shader = sg::create_compute_shader(*device, get_shader_path(shader_name(compute_shaders[0], i).c_str()));
				pipeline.per_instance_compute_pipeline = device->create_pipeline(desc, global_binding_desc);
				seAssert(pipeline.per_instance_compute_pipeline != nullptr, "Failed to create pipeline");

				desc.compute_shader = sg::create_compute_shader(*device, get_shader_path(shader_name(compute_shaders[1], i).c_str()));
				pipeline.build_indirect_arguments_pipeline = device->create_pipeline(desc, global_binding_desc);
				seAssert(pipeline.build_indirect_arguments_pipeline != nullptr, "Failed to create pipeline");
			}

			// Mesh Shader: one pipeline per render bucket
			for (sg::u32 bucket = 0; bucket < GDR_BUCKET_COUNT; bucket++)
			{
				PipelineDesc::Mesh desc;

				desc.mesh_shader = sg::create_mesh_shader(*device, get_shader_path(shader_name(bucket_shaders[bucket][0], i).c_str()));
				desc.pixel_shader = sg::create_pixel_shader(*device, get_shader_path(shader_name(bucket_shaders[bucket][1], i).c_str()));
				desc.render_target_count = 1;
				desc.render_target_format_list[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // TODO - UNDO HARDCODING
				desc.depth_stencil_format = DXGI_FORMAT_D32_FLOAT; // TODO - UNDO HARDCODING

				desc.depth_stencil_desc.depth_enable = render_options.depth_enable;
				desc.depth_stencil_desc.depth_write = render_options.depth_write;

				const ComparisonFunction dcf = render_options.depth_comparison != 0 ? (ComparisonFunction)render_options.depth_comparison : ComparisonFunction::Never; // Not actually supported to pass None..
				desc.depth_stencil_desc.comparison_function = dcf;
				// Double sided foliage is never back face culled
				desc.rasterizer_desc.cull_mode = bucket == GDR_BUCKET_FOLIAGE ? Rasterizer::CullMode::None : (Rasterizer::CullMode) render_options.cull_mode;
				desc.rasterizer_desc.fill_mode = render_options.render_wireframe ? Rasterizer::FillMode::Wireframe : Rasterizer::FillMode::Solid;

				pipeline.mesh_render_pipelines[bucket] = device->create_pipeline(desc, global_binding_desc);
				seAssert(pipeline.mesh_render_pipelines[bucket] != nullptr, "Failed to create pipeline");
			}
		}
	}

	void Scene::apply_test_settings(const nlohmann::json& s)
	{
		// New keys must also be listed in TestRunner.cpp validate_case
		ShaderStructs::DebugData& d = debug_data.shader_debug_data;
		d.debug_mode = s.value("debug_mode", 0u);
		d.cull_all_instances = s.value("cull_all", 0u);
		d.freeze_culling_camera = 0;
		d.use_frustum_culling = s.value("frustum_culling", 1u);
		d.use_cone_culling = s.value("cone_culling", 1u);
		d.sort_mode = s.value("sort_mode", 1u);
		d.debug_render_mode = static_cast<ShaderStructs::DebugRenderMode>(s.value("render_mode", 0u));
		render_options.render_wireframe = s.value("wireframe", false);
		render_options.depth_enable = s.value("depth_enable", true);
		render_options.depth_write = s.value("depth_write", true);
		const std::vector<bool> buckets = s.value("render_buckets", std::vector<bool>{ true, true, true });
		for (sg::u32 bucket = 0; bucket < GDR_BUCKET_COUNT; bucket++)
		{
			render_options.render_bucket[bucket] = bucket < buckets.size() ? buckets[bucket] : true;
		}
		render_options.recreate_pipelines = true;
	}

	ShaderStructs::DebugFeedbackData Scene::read_debug_feedback() const
	{
		ShaderStructs::DebugFeedbackData feedback = {};
		if (debug_data.shader_debug_data.debug_mode != 0) // Otherwise the buffer holds an older debug frame's results
		{
			debug_readback_buffer->read_memory(0, &feedback, sizeof(feedback));
		}
		return feedback;
	}

	void Scene::imgui()
	{
		// Note: ImGui::End() must be called whether or not Begin() returns true.
		place_window_first_use(-400.0f, 10.0f, 380.0f, 430.0f);
		if (ImGui::Begin("GDR Scene"))
		{
			if (ImGui::CollapsingHeader("Debug Options", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Checkbox("Debug Mode", (bool*)&debug_data.shader_debug_data.debug_mode);
				ImGui::BeginDisabled(debug_data.shader_debug_data.debug_mode == 0);
				ImGui::Indent();
				ImGui::Checkbox("Cull All Instances", (bool*)&debug_data.shader_debug_data.cull_all_instances);
				ImGui::Checkbox("Freeze Culling Camera", (bool*)&debug_data.shader_debug_data.freeze_culling_camera);
				ImGui::Checkbox("Use Frustum Culling", (bool*)&debug_data.shader_debug_data.use_frustum_culling);
				ImGui::Checkbox("Use Cone Culling", (bool*)&debug_data.shader_debug_data.use_cone_culling);
				ImGui::Text("Render Mode");
				ImGui::Combo("Debug Render Mode", (int*)&debug_data.shader_debug_data.debug_render_mode, "None\0Sort Order\0Albedo\0Normals\0Lighting\0");
				ImGui::Unindent();
				ImGui::EndDisabled();
			}

			if (ImGui::CollapsingHeader("Sorting Options", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::RadioButton("No Sorting", (int*)&debug_data.shader_debug_data.sort_mode, 0);
				ImGui::RadioButton("Sort Front to Back", (int*)&debug_data.shader_debug_data.sort_mode, 1);
				ImGui::RadioButton("Sort Back to Front", (int*)&debug_data.shader_debug_data.sort_mode, 2);
			}

			if (ImGui::CollapsingHeader("Rendering Options", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Pipeline");
				ImGui::Indent();
				render_options.recreate_pipelines |= ImGui::Checkbox("Wireframe", &render_options.render_wireframe);
				render_options.recreate_pipelines |= ImGui::Checkbox("Depth Enable", &render_options.depth_enable);
				render_options.recreate_pipelines |= ImGui::Checkbox("Depth Write", &render_options.depth_write);
				render_options.recreate_pipelines |= ImGui::Combo("Cull Mode", &render_options.cull_mode, "None\0Front\0Back\0", 3);
				render_options.recreate_pipelines |= ImGui::Combo("Depth Comparison", &render_options.depth_comparison, "None\0Never\0Less\0Equal\0LessEqual\0Greater\0NotEqual\0GreaterEqual\0Always\0");
				ImGui::Unindent();
				ImGui::Text("Render Buckets (one indirect DispatchMesh each)");
				ImGui::Indent();
				ImGui::Checkbox("Opaque", &render_options.render_bucket[GDR_BUCKET_OPAQUE]);
				ImGui::Checkbox("Foliage (alpha tested, double sided)", &render_options.render_bucket[GDR_BUCKET_FOLIAGE]);
				ImGui::Checkbox("Terrain", &render_options.render_bucket[GDR_BUCKET_TERRAIN]);
				ImGui::Unindent();
			}

			if (ImGui::CollapsingHeader("Environment"))
			{
				// Saved with the scene ("environment"); lights are node components
				SceneEnvironment& e = properties.environment;
				ImGui::ColorEdit3("Sky Ambient", &e.sky_colour.x);
				ImGui::ColorEdit3("Ground Ambient", &e.ground_colour.x);
				ImGui::SliderFloat("Ambient Intensity", &e.ambient_intensity, 0.0f, 2.0f);
				ImGui::ColorEdit3("Fog Colour", &e.fog_colour.x);
				ImGui::SliderFloat("Fog Density", &e.fog_density, 0.0f, 0.05f, "%.4f");
				ImGui::SliderFloat("Fog Start", &e.fog_start, 0.0f, 200.0f);
				ImGui::SliderFloat("Exposure", &e.exposure, 0.1f, 4.0f);
			}
		}
		ImGui::End();

		place_window_first_use(-400.0f, 450.0f, 380.0f, 320.0f);
		if (ImGui::Begin("GDR Statistics"))
		{
			if (ImGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Instances:  %u", statistics.total_instances);
				ImGui::Text("Mesh Parts: %u", statistics.total_mesh_parts);
				ImGui::Text("Meshlets:   %u", statistics.total_meshlets);
				ImGui::Text("Terrain Tiles: %u", statistics.terrain_tiles);
				ImGui::Text("Lights:     %u", statistics.total_lights);
				ImGui::Text("Meshlets per bucket: opaque %u, foliage %u, terrain %u", statistics.bucket_meshlets[GDR_BUCKET_OPAQUE], statistics.bucket_meshlets[GDR_BUCKET_FOLIAGE], statistics.bucket_meshlets[GDR_BUCKET_TERRAIN]);
				ImGui::Text("Texture Slices: %u (%ux%u)", statistics.texture_slices, MaterialTextures::kSliceSize, MaterialTextures::kSliceSize);

				ImGui::Text("GPU Feedback Data:");
				if (debug_data.shader_debug_data.debug_mode == 0)
				{
					ImGui::SameLine(); ImGui::Text("(Requires Debug Mode Enabled)");
				}
				ImGui::BeginDisabled(debug_data.shader_debug_data.debug_mode == 0);
				ImGui::Indent();
				const float culled_percent = statistics.total_meshlets > 0 ? (static_cast<float>(statistics.raw_feedback_data.meshlets_culled_total) / static_cast<float>(statistics.total_meshlets)) * 100.0f : 0.0f;
				ImGui::Text("Meshlets Culled (total):    %u/%u (%0.1f%%)", statistics.raw_feedback_data.meshlets_culled_total, statistics.total_meshlets, culled_percent);
				ImGui::Text("Meshlets Culled By Frustum: %u", statistics.raw_feedback_data.meshlets_cullled_by_frustum);
				ImGui::Text("Meshlets Culled By Cone:    %u", statistics.raw_feedback_data.meshlets_culled_by_cone);				
				ImGui::Unindent();
				ImGui::EndDisabled();
			}
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
		if (!model_buffers.material_textures.is_uploaded())
		{
			model_buffers.material_textures.upload(device, upload_heap);
		}
		update_lighting(scene_graph_traverser, upload_heap);

		// Construct and upload per-instance data
		model_buffers.instance_data.cpu_data = {};
		per_instance_render_list = {};

		statistics.total_instances = 0;
		statistics.total_mesh_parts = 0;
		statistics.terrain_tiles = 0;
		std::fill(std::begin(statistics.bucket_meshlets), std::end(statistics.bucket_meshlets), 0u);
		statistics.texture_slices = model_buffers.material_textures.get_slice_count();
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
				const ModelBuffers::ModelData* per_model_data_ptr = model_buffers.find_model(model_name);
				if (per_model_data_ptr == nullptr)
				{
					continue; // Model failed to load; its instances are skipped
				}
				const ModelBuffers::ModelData& per_model_data = *per_model_data_ptr;

				statistics.total_instances += static_cast<sg::u32>(model_list.size());
				statistics.total_mesh_parts += static_cast<sg::u32>(model_list.size() * per_model_data.mesh_parts.size());
				if (is_terrain_model_name(model_name))
				{
					for (const auto& mesh_part : per_model_data.mesh_parts)
					{
						statistics.terrain_tiles += static_cast<sg::u32>(model_list.size()) * mesh_part.meshlet_count;
					}
				}

				// TODO: Not ideal to treat each mesh part as its own instances, wastes space and data
				for (const auto& mesh_part : per_model_data.mesh_parts)
				{
					if (mesh_part.meshlet_count == 0)
					{
						continue; // Nothing to render (and would divide by zero in the per-instance shader)
					}
					PerInstanceRenderInfo& instance_render_info = per_instance_render_list.emplace_back();
					instance_render_info.instance_offset = model_buffers.instance_data.cpu_data.size();
					instance_render_info.count = model_list.size();
					instance_render_info.meshlet_offset = mesh_part.meshlet_data_offset;
					instance_render_info.meshlets_per_instance = mesh_part.meshlet_count;
					instance_render_info.bucket = mesh_part.bucket;
					statistics.bucket_meshlets[mesh_part.bucket] += static_cast<sg::u32>(model_list.size()) * mesh_part.meshlet_count;

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
		}

		if (per_instance_render_list.size())
		{
			model_buffers.instance_data.create_and_upload_to_gpu(device, upload_heap);

			// One region of the visible meshlet list per render bucket. Regions are bitonic sorted on their own, so each is a power
			// of two (and at least one 32 element sort tile) in size.
			sg::u32 output_size = 0;
			for (sg::u32 bucket = 0; bucket < GDR_BUCKET_COUNT; bucket++)
			{
				const sg::u32 count = statistics.bucket_meshlets[bucket];
				bucket_regions[bucket].offset = output_size;
				bucket_regions[bucket].size = count > 0 ? std::max<sg::u32>(std::bit_ceil(count), 32u) : 0u;
				output_size += bucket_regions[bucket].size;
			}
			// Only reallocated when it grows
			model_buffers.gpu_pre_process_instances_output.create(device, output_size);
		}
		
		statistics.total_meshlets = total_meshlets;

		// Statistics
		if (debug_data.shader_debug_data.debug_mode != 0)
		{
			debug_readback_buffer->read_memory(0, &statistics.raw_feedback_data, sizeof(statistics.raw_feedback_data));
		}
		else
		{
			statistics.raw_feedback_data = {};
		}
	}

	void Scene::render	(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw, sg::ShaderResourceView depth_view)
	{
		using namespace sg;

		// Stage 1: Compute dispatch for each instance type for the per-instance-pre-pass
		// Stage TODO: Sorting Step
		// Stage 2: Execute Indirect DispatchMesh

		// TODO - Add a coarse per-instance culling pass, not just per-meshlet initially (or a full BVH test for a large complex scene)

		// Nothing in the scene to render
		if (per_instance_render_list.size() == 0)
			return;

		command_list.clear_buffer_uint(model_buffers.gpu_pre_process_instances_output.uav, model_buffers.gpu_pre_process_instances_output.srv, 0);
		command_list.clear_buffer_uint(model_buffers.atomic_counter.uav, model_buffers.atomic_counter.srv, 0);
		command_list.clear_buffer_uint(model_buffers.indirect_arguments.uav, model_buffers.indirect_arguments.srv, 0);

		Binding b;
		b.uav_binding_count = 3;
		b.srv_binding_count = SRV_COUNT;
		b.cbv_binding_count = 2;
		b.sampler_binding_count = 4;

		// Updated Culling Camera Data
		if (debug_data.shader_debug_data.debug_mode == 0 || (debug_data.shader_debug_data.debug_mode != 0 && debug_data.shader_debug_data.freeze_culling_camera == 0))
		{
			// Converting non-gdr cameradata to gdr camera data
			debug_data.culling_camera_data = (const ShaderStructs::CameraData&)camera.GetCameraShaderData();
			debug_data.culling_camera_frustum = camera.GetCameraBoundingFrustum();
		}
		
		if (debug_data.shader_debug_data.debug_mode != 0 && debug_data.shader_debug_data.freeze_culling_camera != 0)
		{
			debug_draw.DrawFrustum(DebugDraw::ColourRGBA(255, 0, 0, 255), debug_data.culling_camera_frustum);
		}

		if (debug_data.shader_debug_data.debug_mode != 0)
		{
			b.cbv_binding_count = 4;
			ConstantBufferView shader_debug_data_cbv = cbuffer.AllocateAndWrite(debug_data.shader_debug_data);
			b.set_cbv(shader_debug_data_cbv, 2);

			ConstantBufferView culling_camera_data_cbv = cbuffer.AllocateAndWrite(debug_data.culling_camera_data);
			b.set_cbv(culling_camera_data_cbv, 3);

			b.uav_binding_count = 4;
			b.set_uav(debug_feedback_uav, 3);

			// Clear our feedback uav to zero
			sg::ShaderResourceView srv = {};
			command_list.clear_buffer_uint(debug_feedback_uav, srv, 0);
		}
				
		b.set_uav(model_buffers.gpu_pre_process_instances_output.uav, 0);
		b.set_uav(model_buffers.atomic_counter.uav, 1);
		b.set_uav(model_buffers.indirect_arguments.uav, 2);

		b.set_srv(model_buffers.vertex_data.srv, SRV_VERTICES);
		b.set_srv(model_buffers.meshlet_data.srv, SRV_MESHLETS);
		b.set_srv(model_buffers.meshlet_vertex_index.srv, SRV_MESHLET_VERTEX_INDICES);
		b.set_srv(model_buffers.meshlet_primitive_index.srv, SRV_MESHLET_PRIMITIVES);
		b.set_srv(model_buffers.instance_data.srv, SRV_INSTANCES);
		b.set_srv(model_buffers.material_data.srv, SRV_MATERIALS);
		b.set_srv(light_data.srv, SRV_LIGHTS);
		b.set_srv(environment_data.srv, SRV_ENVIRONMENT);
		b.set_srv(model_buffers.terrain_data.srv, SRV_TERRAINS);
		b.set_srv(model_buffers.terrain_heights.srv, SRV_TERRAIN_HEIGHTS);
		b.set_srv(model_buffers.material_textures.get_srv(), SRV_MATERIAL_TEXTURES);
		
		b.set_cbv(cbv_camera, 0);

		command_list.set_pipeline(pipelines[debug_data.shader_debug_data.debug_mode].per_instance_compute_pipeline.get());

		for (const PerInstanceRenderInfo& instance_section : per_instance_render_list)
		{
			ShaderStructs::PerInstanceProcessingData pre_process_cbuffer = {};
			pre_process_cbuffer.instance_offset = instance_section.instance_offset;
			pre_process_cbuffer.meshlets_per_instance = instance_section.meshlets_per_instance;
			pre_process_cbuffer.meshlet_offset = instance_section.meshlet_offset;
			pre_process_cbuffer.meshlets_to_process_count = instance_section.count * instance_section.meshlets_per_instance;
			pre_process_cbuffer.bucket = instance_section.bucket;
			pre_process_cbuffer.bucket_output_offset = bucket_regions[instance_section.bucket].offset;
			pre_process_cbuffer.cone_culling = instance_section.bucket == GDR_BUCKET_FOLIAGE ? 0u : 1u;
			
			ConstantBufferView cbv = cbuffer.AllocateAndWrite(pre_process_cbuffer);
			b.set_cbv(cbv, 1);

			command_list.bind(b, PipelineType::Compute);
			command_list.dispatch(sg::AlignUp<uint32_t>(pre_process_cbuffer.meshlets_to_process_count, 32u) / 32u, 1, 1);
		}
		// The binding is reused below for the indirect argument build (the per-instance CBV is unused by that pass).

		// Stage 1.5 - Build indirect arguments
		command_list.set_pipeline(pipelines[debug_data.shader_debug_data.debug_mode].build_indirect_arguments_pipeline.get());
		command_list.dispatch(1, 1, 1);

		// Stage 2 - Optional sorting stage: each bucket's region separately
		if (debug_data.shader_debug_data.sort_mode != 0)
		{
			for (sg::u32 bucket = 0; bucket < GDR_BUCKET_COUNT; bucket++)
			{
				if (bucket_regions[bucket].size == 0)
				{
					continue;
				}
				gpu_sort.sort(command_list, cbuffer, model_buffers.gpu_pre_process_instances_output.uav, model_buffers.atomic_counter.uav, bucket_regions[bucket].size,
					debug_data.shader_debug_data.sort_mode == 1 ? GPUSort::Ascending : GPUSort::Descending, bucket_regions[bucket].offset, bucket);
			}
		}

		// Stage 3
		// So at this point we now have a list of final meshlets to be rendered in model_buffers.gpu_pre_process_instances_output,
		// grouped by render bucket. Each bucket is one indirect DispatchMesh with its own pipeline state and shaders.
		const Pipelines& render_pipelines = pipelines[debug_data.shader_debug_data.debug_mode];
		mesh_shader_indirect_arguments->set_indirect_buffer(model_buffers.indirect_arguments.buffer);
		for (sg::u32 bucket = 0; bucket < GDR_BUCKET_COUNT; bucket++)
		{
			if (bucket_regions[bucket].size == 0 || !render_options.render_bucket[bucket])
			{
				continue;
			}
			ShaderStructs::PerInstanceProcessingData bucket_cbuffer = {};
			bucket_cbuffer.bucket = bucket;
			bucket_cbuffer.bucket_output_offset = bucket_regions[bucket].offset;
			ConstantBufferView cbv = cbuffer.AllocateAndWrite(bucket_cbuffer);
			b.set_cbv(cbv, 1);

			command_list.set_pipeline(render_pipelines.mesh_render_pipelines[bucket].get());
			command_list.bind(b, PipelineType::Geometry);
			command_list.execute_indirect(*mesh_shader_indirect_arguments, bucket * sizeof(std::array<sg::u32, 4>));
		}

		// Copy our feedback buffer to our readback buffer
		if (debug_data.shader_debug_data.debug_mode != 0)
		{
			command_list.copy_buffer_to_buffer(debug_readback_buffer.get(), debug_feedback_buffer.get());
		}
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
			// Point/line meshes have no normals even with aiProcess_GenSmoothNormals.
			v.Normal = assimp_mesh->mNormals ? XMFLOAT3(&assimp_mesh->mNormals[vert_idx].x) : XMFLOAT3(0, 0, 0);
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

	bool Scene::load_model(std::string path, ShaderStructs::ModelData& gpu_model_data)
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
		if (!aScene)
		{
			seWriteLine("Failed To Load Assimp Scene %s\n%s", path.c_str(), ai_importer.GetErrorString());
			return false;
		}

		ModelBuffers::ModelData& new_model = model_buffers.models.emplace_back();
		new_model.name = path;

		new_model.mesh_parts.resize(aScene->mNumMeshes);

		// Materials are appended to the scene wide material buffer; meshlets refer to them by global index
		const sg::u32 material_base = static_cast<sg::u32>(model_buffers.material_data.cpu_data.size());
		for (unsigned int material_idx = 0; material_idx < aScene->mNumMaterials; material_idx++)
		{
			model_buffers.material_data.cpu_data.push_back(load_material(*aScene, *aScene->mMaterials[material_idx], path, model_buffers.material_textures));
		}
		new_model.material_count = aScene->mNumMaterials;
		if (aScene->mNumMaterials == 0)
		{
			model_buffers.material_data.cpu_data.push_back(default_material());
		}

		for (size_t mesh_part_idx = 0; mesh_part_idx < new_model.mesh_parts.size(); mesh_part_idx++)
		{
			ModelBuffers::ModelData::MeshPart& mesh_part = new_model.mesh_parts[mesh_part_idx];
			mesh_part = {};

			mesh_part.vertex_data_offset = model_buffers.vertex_data.cpu_data.size();
			mesh_part.meshlet_data_offset = model_buffers.meshlet_data.cpu_data.size();
			mesh_part.meshlet_vertex_index_offset = model_buffers.meshlet_vertex_index.cpu_data.size();
			mesh_part.meshlet_primitive_index_offset = model_buffers.meshlet_primitive_index.cpu_data.size();

			aiMesh* assimp_mesh = aScene->mMeshes[mesh_part_idx];
			mesh_part.material_index = material_base + (aScene->mNumMaterials > 0 ? assimp_mesh->mMaterialIndex : 0);
			mesh_part.bucket = bucket_of_material(model_buffers.material_data.cpu_data[mesh_part.material_index]);

			// Load normal vertex and index buffer data (only used to generate meshlet data from
			std::vector<Vertex> vertices = load_vertex_buffer(assimp_mesh);
			std::vector<uint32_t> indices = load_index_buffer(assimp_mesh);

			// Process vertices for entire mesh-part
			{
				// TODO
			}

			// Create meshlets (parts without triangles, e.g. point/line meshes, produce none)
			if (indices.size() >= 3 && vertices.size() > 0)
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
				if (meshlet_count > 0)
				{
					const meshopt_Meshlet& last = tmp_meshlets[meshlet_count - 1];
					tmp_meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
					tmp_meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3)); // Align total to be multiple of 4
				}
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
					gpu_meshlet.material_index		 = mesh_part.material_index;
					gpu_meshlet.meshlet_type		 = GDR_MESHLET_TYPE_MESH;

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

					// Sphere & Cone Culling data
					{
						meshopt_Bounds meshlet_bounds = meshopt_computeMeshletBounds(
							&tmp_meshlet_vertices[meshlet.vertex_offset],
							&tmp_meshlet_triangles[meshlet.triangle_offset],
							meshlet.triangle_count,
							(float*)vertices.data(),
							vertices.size(),
							sizeof(vertices[0]));

						gpu_meshlet.spherepos_xyz_radius_w = DirectX::XMFLOAT4A(meshlet_bounds.center[0], meshlet_bounds.center[1], meshlet_bounds.center[2], meshlet_bounds.radius);
						gpu_meshlet.cone_apex_xyz = DirectX::XMFLOAT4A(meshlet_bounds.cone_apex[0], meshlet_bounds.cone_apex[1], meshlet_bounds.cone_apex[2], 0.0f);

						gpu_meshlet.cone_axis_xyz_cone_cutoff_w = DirectX::XMFLOAT4A(meshlet_bounds.cone_axis[0], meshlet_bounds.cone_axis[1], meshlet_bounds.cone_axis[2], meshlet_bounds.cone_cutoff);

						char* cone_axis = (char*)&gpu_meshlet.cone_axis_s8xyz_cone_cutoff_s8w;
						cone_axis[0] = meshlet_bounds.cone_axis_s8[0];
						cone_axis[1] = meshlet_bounds.cone_axis_s8[1];
						cone_axis[2] = meshlet_bounds.cone_axis_s8[2];
						cone_axis[3] = meshlet_bounds.cone_cutoff_s8;						
					}

					// AABB & OBB Data
					{
						DirectX::XMFLOAT3 bounding_box_min = { FLT_MAX, FLT_MAX, FLT_MAX };
						DirectX::XMFLOAT3 bounding_box_max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
						DirectX::BoundingBox aabb = {};
						DirectX::BoundingOrientedBox obb = {};

						// Every vertex referenced by the meshlet's triangles is in its unique vertex list, so iterate that.
						for (size_t i = 0; i < meshlet.vertex_count; i++)
						{
							const size_t vertex_index = tmp_meshlet_vertices[meshlet.vertex_offset + i];
							const Vertex& vertex = vertices[vertex_index];

							bounding_box_max = Max(bounding_box_max, vertex.Position);
							bounding_box_min = Min(bounding_box_min, vertex.Position);
						}

						const float dist_x = fabsf(bounding_box_max.x - bounding_box_min.x);
						const float dist_y = fabsf(bounding_box_max.y - bounding_box_min.y);
						const float dist_z = fabsf(bounding_box_max.z - bounding_box_min.z);
						aabb.Center.x = bounding_box_min.x + (dist_x / 2);
						aabb.Center.y = bounding_box_min.y + (dist_y / 2);
						aabb.Center.z = bounding_box_min.z + (dist_z / 2);
						aabb.Extents.x = dist_x / 2;
						aabb.Extents.y = dist_y / 2;
						aabb.Extents.z = dist_z / 2;

						// Initial OBB is from AABB
						DirectX::BoundingOrientedBox::CreateFromBoundingBox(obb, aabb);

						// Now set GPU Data Structure
						gpu_meshlet.aabb_center = DirectX::XMFLOAT4A(aabb.Center.x, aabb.Center.y, aabb.Center.z, 0.0f);
						gpu_meshlet.aabb_extents = DirectX::XMFLOAT4A(aabb.Extents.x, aabb.Extents.y, aabb.Extents.z, 0.0f);

						gpu_meshlet.obb_center = DirectX::XMFLOAT4A(obb.Center.x, obb.Center.y, obb.Center.z, 0.0f);
						gpu_meshlet.obb_extents = DirectX::XMFLOAT4A(obb.Extents.x, obb.Extents.y, obb.Extents.z, 0.0f);
						gpu_meshlet.obb_orientation = DirectX::XMFLOAT4A(&obb.Orientation.x);
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
		return true;
	}

	bool Scene::load_terrain(const std::string& name, const TerrainDesc& desc)
	{
		TerrainBuild build;
		if (!build_terrain(desc, build))
		{
			return false;
		}

		const sg::u32 terrain_index = static_cast<sg::u32>(model_buffers.terrain_data.cpu_data.size());
		build.data.height_offset = static_cast<sg::u32>(model_buffers.terrain_heights.cpu_data.size());
		model_buffers.terrain_data.cpu_data.push_back(build.data);
		model_buffers.terrain_heights.cpu_data.insert(model_buffers.terrain_heights.cpu_data.end(), build.heights.begin(), build.heights.end());

		ShaderStructs::MaterialData material = default_material();
		material.flags = GDR_MATERIAL_TERRAIN;
		material.terrain_index = terrain_index;
		material.specular = ShaderStructs::float4(0.02f, 0.02f, 0.02f, 16.0f);
		const sg::u32 material_index = static_cast<sg::u32>(model_buffers.material_data.cpu_data.size());
		model_buffers.material_data.cpu_data.push_back(material);

		// A terrain is a model with one mesh part whose meshlets are its tiles
		ModelBuffers::ModelData& model = model_buffers.models.emplace_back();
		model.name = name;
		model.material_count = 1;
		ModelBuffers::ModelData::MeshPart& part = model.mesh_parts.emplace_back();
		part.material_index = material_index;
		part.bucket = GDR_BUCKET_TERRAIN;
		part.meshlet_data_offset = static_cast<sg::u32>(model_buffers.meshlet_data.cpu_data.size());
		part.meshlet_count = static_cast<sg::u32>(build.tiles.size());
		part.bounding_box_min = build.bounds_min;
		part.bounding_box_max = build.bounds_max;
		for (ShaderStructs::MeshletData& tile : build.tiles)
		{
			tile.material_index = material_index;
			tile.terrain_index = terrain_index;
			model_buffers.meshlet_data.cpu_data.push_back(tile);
		}
		seWriteLine("GDR: terrain %u x %u quads, %zu tiles", build.data.quads, build.data.quads, build.tiles.size());
		return true;
	}

	void Scene::update_lighting(const SceneGraphTraverser& scene_graph, Ptr<UploadHeap>& upload_heap)
	{
		using namespace DirectX;
		// Colours are authored in gamma space; lighting is computed in linear space
		auto linear = [](const Float3& c, float scale)
			{
				return ShaderStructs::float4(std::pow(c.x, 2.2f) * scale, std::pow(c.y, 2.2f) * scale, std::pow(c.z, 2.2f) * scale, 1.0f);
			};

		light_data.cpu_data.clear();
		for (const SceneGraphTraverser::LightInstance& instance : scene_graph.lights)
		{
			if (light_data.cpu_data.size() == GDR_MAX_LIGHTS)
			{
				if (!reported_light_limit)
				{
					seWriteLine("GDR: the scene has %zu lights; only the first %u are used", scene_graph.lights.size(), GDR_MAX_LIGHTS);
					reported_light_limit = true;
				}
				break;
			}
			const LightDesc& desc = *instance.light;
			ShaderStructs::LightData& light = light_data.cpu_data.emplace_back();
			light = {};
			light.type = desc.type == "directional" ? GDR_LIGHT_DIRECTIONAL : GDR_LIGHT_POINT;
			light.colour = linear(desc.colour, desc.intensity);
			XMFLOAT3 position;
			XMStoreFloat3(&position, instance.world_matrix.r[3]);
			light.position_range = ShaderStructs::float4(position.x, position.y, position.z, std::max(desc.range, 1e-3f));
			XMFLOAT3 direction;
			XMStoreFloat3(&direction, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(desc.direction.x, desc.direction.y, desc.direction.z, 0.0f), instance.world_matrix)));
			light.direction = ShaderStructs::float4(direction.x, direction.y, direction.z, 0.0f);
		}
		statistics.total_lights = static_cast<sg::u32>(light_data.cpu_data.size());
		if (light_data.cpu_data.empty())
		{
			light_data.cpu_data.emplace_back() = {}; // Keeps the buffer bindable; light_count is 0
		}

		const SceneEnvironment& e = properties.environment;
		ShaderStructs::EnvironmentData environment = {};
		environment.sky_colour = linear(e.sky_colour, e.ambient_intensity);
		environment.ground_colour = linear(e.ground_colour, e.ambient_intensity);
		environment.fog_colour = linear(e.fog_colour, 1.0f);
		environment.fog_colour.w = std::max(e.fog_density, 0.0f);
		environment.fog_start = std::max(e.fog_start, 0.0f);
		environment.exposure = std::max(e.exposure, 0.0f);
		environment.light_count = statistics.total_lights;
		environment_data.cpu_data = { environment };

		light_data.create_and_upload_to_gpu(device, upload_heap);
		environment_data.create_and_upload_to_gpu(device, upload_heap);
	}

	void Scene::load_gpu_data(SceneGraphTraverser& scene_graph, Ptr<UploadHeap>& upload_heap)
	{
		bool recreate_gpu_buffers = false;

		// Models that previously failed to import are not expected in model_buffers
		size_t expected_model_count = 0;
		for (auto& [model_string, model_list] : scene_graph.model_instance_map)
		{
			if (failed_model_paths.count(model_string) == 0) expected_model_count++;
		}

		if (expected_model_count != model_buffers.models.size())
		{
			recreate_gpu_buffers = true;
		}
		else
		{
		for (auto& [model_string, model_list] : scene_graph.model_instance_map)
			{
				if (failed_model_paths.count(model_string) != 0)
				{
					continue;
				}
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
			if (failed_model_paths.count(model_string) != 0)
			{
				continue;
			}
			if (is_terrain_model_name(model_string))
			{
				if (!load_terrain(model_string, *scene_graph.terrains.at(model_string)))
				{
					failed_model_paths.insert(model_string);
				}
				continue;
			}
			ShaderStructs::ModelData model_gpu_data = {};
			if (load_model(model_string, model_gpu_data))
			{
				model_buffers.model_data.cpu_data.push_back(model_gpu_data);
			}
			else
			{
				failed_model_paths.insert(model_string);
			}
		}

		// Upload all model data to GPU
		model_buffers.create_and_upload_to_gpu(device, upload_heap);
	}

	void Scene::generate_hzb(sg::CommandList& command_list, sg::ShaderResourceView depth_view, SimpleLinearConstantBuffer& cbuffer)
	{
		// Generate HZB
		gpu_hzb.generate_hzb(device, command_list, depth_view, cbuffer);
	}

	// Scene Graph Traverser
	void SceneGraphTraverser::process_scene_graph(SceneNode& root_node)
	{
		auto process_node = [this](const SceneNode& scene_node)
			{
				const bool has_model = scene_node.model_name != kINVALID_MODEL_NAME;
				if (!has_model && !scene_node.terrain && !scene_node.light)
				{
					return;
				}

				using namespace DirectX;

				InstanceData node_data;
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

				if (has_model)
				{
					model_instance_map[scene_node.model_name].push_back(node_data);
				}
				if (scene_node.terrain)
				{
					const std::string terrain_name = terrain_model_name(*scene_node.terrain);
					model_instance_map[terrain_name].push_back(node_data);
					terrains.emplace(terrain_name, &*scene_node.terrain);
				}
				if (scene_node.light)
				{
					lights.push_back({ node_data.model_matrix, &*scene_node.light });
				}
			};

		scene_node_traverse(root_node, process_node);
	}


	const ModelBuffers::ModelData* ModelBuffers::find_model(std::string_view name)
	{
		for (const auto& model : models)
		{
			if (model.name == name)
			{
				return &model;
			}
		}
		return nullptr;
	}

	void ModelBuffers::create_and_upload_to_gpu(sg::SharedPtr<sg::Device>& device, sg::Ptr<sg::UploadHeap>& upload_heap)
	{
		// Every buffer the shaders bind gets at least one element (a scene of only terrains has no vertices, for example)
		if (material_data.cpu_data.empty()) material_data.cpu_data.push_back(default_material());
		if (terrain_data.cpu_data.empty()) terrain_data.cpu_data.emplace_back() = {};
		if (terrain_heights.cpu_data.empty()) terrain_heights.cpu_data.push_back(0.0f);
		if (vertex_data.cpu_data.empty()) vertex_data.cpu_data.emplace_back() = {};
		if (meshlet_vertex_index.cpu_data.empty()) meshlet_vertex_index.cpu_data.push_back(0);
		if (meshlet_primitive_index.cpu_data.empty()) meshlet_primitive_index.cpu_data.push_back(0);

		meshlet_data.create_and_upload_to_gpu(device, upload_heap);
		material_data.create_and_upload_to_gpu(device, upload_heap);
		vertex_data.create_and_upload_to_gpu(device, upload_heap);
		meshlet_vertex_index.create_and_upload_to_gpu(device, upload_heap);
		meshlet_primitive_index.create_and_upload_to_gpu(device, upload_heap);
		terrain_data.create_and_upload_to_gpu(device, upload_heap);
		terrain_heights.create_and_upload_to_gpu(device, upload_heap);

		atomic_counter.create(device, GDR_BUCKET_COUNT);
		indirect_arguments.create(device, GDR_BUCKET_COUNT);
	}
}