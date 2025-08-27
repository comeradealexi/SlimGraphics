#include "ModelViewer.h"
#include <sgPlatformInclude.h>
#include <imgui.h>
#include <seEngineBasicFileIO.h>
#include "Camera.h"


using namespace sg;

inline uint32_t DivRoundUp(uint32_t num, uint32_t den) { return (num + den - 1) / den; }

ModelViewer::ModelViewer(SharedPtr<Device>& _device) : render_target_format(DXGI_FORMAT_R8G8B8A8_UNORM), depth_stencil_format(DXGI_FORMAT_D32_FLOAT), device(_device)
{
	if (!device->SupportsMeshShaders())
	{
		render_as_mesh_shader = false;
	}

	pipeline_binding_desc = {};
	pipeline_binding_desc.cbv_binding_count = 2;
	pipeline_binding_desc.uav_binding_count = 1;

	mesh_shading.binding_desc = {};
	mesh_shading.binding_desc.cbv_binding_count = 2;
	mesh_shading.binding_desc.srv_binding_count = 5;
	mesh_shading.binding_desc.uav_binding_count = 1;

	amplification_mesh_shading.binding_desc = {};
	amplification_mesh_shading.binding_desc.cbv_binding_count = 2;
	amplification_mesh_shading.binding_desc.srv_binding_count = 5;
	amplification_mesh_shading.binding_desc.uav_binding_count = 1;

	{
		std::vector<uint8_t> vertex_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\ModelViewer_VertexShader.PC_DXC");
		std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\ModelViewer_PixelShader.PC_DXC");
		shader_vertex = device->create_vertex_shader(vertex_data);
		shader_pixel = device->create_pixel_shader(pixel_data);

		vertex_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\ModelViewer_VertexShaderTriangle.PC_DXC");
		shader_vertex_triangle = device->create_vertex_shader(vertex_data);

		vertex_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\ModelViewer_VertexShaderQuad.PC_DXC");
		shader_vertex_quad = device->create_vertex_shader(vertex_data);
	}

	// Mesh Shader
	{
		std::vector<uint8_t> mesh_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\ModelViewer_MeshShader.PC_DXC");
		std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\ModelViewer_PixelMeshShader.PC_DXC");
		mesh_shading.shader_mesh = device->create_mesh_shader(mesh_data);
		mesh_shading.shader_pixel = device->create_pixel_shader(pixel_data);
	}

	// Amplification + Mesh Shader
	{
		std::vector<uint8_t> amp_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\ModelViewer_MeshletAmplificationAS.PC_DXC");
		std::vector<uint8_t> mesh_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\ModelViewer_MeshletAmplificationMS.PC_DXC");
		std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\ModelViewer_MeshletAmplificationPS.PC_DXC");
		amplification_mesh_shading.shader_amplification = device->create_amplification_shader(amp_data);
		amplification_mesh_shading.shader_mesh = device->create_mesh_shader(mesh_data);
		amplification_mesh_shading.shader_pixel = device->create_pixel_shader(pixel_data);
	}

	CreatePipeline();

	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/teapot.obj");
	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/cow.obj");
	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/stanford-bunny.obj");
	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/suzanne.obj");
	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/bunny.obj");
	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/bunny_patched.obj");
	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/bunny_decimated.obj");
	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/orb.obj");
	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/tree.obj");
	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/column.obj");
	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/hollowcube.obj");
	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/platform.obj");
	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/cube.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Bartek/donut.glb");

	model_file_list.push_back("../SlimGraphicsAssets/3DBuilder/Bulldozer.obj");
	model_file_list.push_back("../SlimGraphicsAssets/3DBuilder/Carved pumpkin.obj");
	model_file_list.push_back("../SlimGraphicsAssets/3DBuilder/Cat.obj");
	model_file_list.push_back("../SlimGraphicsAssets/3DBuilder/Residential House.obj");
	model_file_list.push_back("../SlimGraphicsAssets/3DBuilder/Sunflower - low poly.obj");
	model_file_list.push_back("../SlimGraphicsAssets/3DBuilder/Tree.obj");
	model_file_list.push_back("../SlimGraphicsAssets/3DBuilder/Trophy cup.obj");
	model_file_list.push_back("../SlimGraphicsAssets/3DBuilder/Tuft of grass.obj");

	model_file_list.push_back("../SlimGraphicsAssets/Blender-LODSpheres/Sphere3x3.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Blender-LODSpheres/Sphere6x6.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Blender-LODSpheres/Sphere9x9.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Blender-LODSpheres/Sphere16x16.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Blender-LODSpheres/Sphere32x32.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Blender-LODSpheres/Sphere64x64.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Blender-LODSpheres/Sphere128x128.obj");

	model_file_list.push_back("../SlimGraphicsAssets/Nvidia/grass.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Nvidia/palm_tree.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Nvidia/sponza.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Nvidia/T34-85.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Nvidia/armadillo.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Nvidia/buddha.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Nvidia/cow.obj");
	model_file_list.push_back("../SlimGraphicsAssets/Nvidia/dragon.obj");

	model_file_list.push_back("../External/assimp-5.3.1/test/models-nonbsd/FBX/2013_BINARY/duck.fbx");
	model_file_list.push_back("../External/assimp-5.3.1/test/models-nonbsd/FBX/2013_BINARY/Cinema4D.fbx");
	model_file_list.push_back("../External/assimp-5.3.1/test/models-nonbsd/FBX/2013_BINARY/COLLADA.fbx");
	model_file_list.push_back("../External/assimp-5.3.1/test/models-nonbsd/FBX/2013_BINARY/ConcavePolygon.fbx");
	model_file_list.push_back("../External/assimp-5.3.1/test/models-nonbsd/FBX/2013_BINARY/Granate.fbx");
	model_file_list.push_back("../External/assimp-5.3.1/test/models-nonbsd/FBX/2013_BINARY/jeep1.fbx");
	model_file_list.push_back("../External/assimp-5.3.1/test/models-nonbsd/FBX/2013_BINARY/mar_rifle.fbx");
	model_file_list.push_back("../External/assimp-5.3.1/test/models-nonbsd/FBX/2013_BINARY/mp5_sil.fbx");
	model_file_list.push_back("../External/assimp-5.3.1/test/models-nonbsd/FBX/2013_BINARY/pyramob.fbx");
	model_file_list.push_back("../External/assimp-5.3.1/test/models-nonbsd/FBX/2013_BINARY/anims_with_full_rotations_between_keys.fbx");
	model_file_list.push_back("../External/assimp-5.3.1/test/models/IFC/AC14-FZK-Haus.ifc");
	model_file_list.push_back("../External/assimp-5.3.1/test/models/MDC/spider.mdc");

	model_file_list.push_back("../External/assimp-5.3.1/test/models/OBJ/WusonOBJ.obj");
	model_file_list.push_back("../External/assimp-5.3.1/test/models-nonbsd/OBJ/rifle.obj");
	model_file_list.push_back("../External/assimp-5.3.1/test/models-nonbsd/OBJ/segment.obj");


	model_file_list.push_back("../SlimGraphicsAssets/GL/bunny.obj");
	model_file_list.push_back("../SlimGraphicsAssets/GL/ChessKing.obj");
	model_file_list.push_back("../SlimGraphicsAssets/GL/ChessPawn.obj");
	model_file_list.push_back("../SlimGraphicsAssets/GL/dragon.obj");
	model_file_list.push_back("../SlimGraphicsAssets/GL/elephant.obj");
	model_file_list.push_back("../SlimGraphicsAssets/GL/monkey.obj");
	model_file_list.push_back("../SlimGraphicsAssets/GL/teapot.obj");
	model_file_list.push_back("../SlimGraphicsAssets/GL/three_objects.obj");
	model_file_list.push_back("../SlimGraphicsAssets/GL/venusm.obj");


	model_init_data.file_path = model_file_list[0];


	uav_memory = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024);
	uav_buffer = device->create_buffer(uav_memory, 64ull * 1024, BufferType::GeneralDataBuffer, true);
	uav = device->create_unordered_access_view(uav_buffer, sizeof(u32), (64ull * 1024) / sizeof(u32));
	srv = device->create_shader_resource_view(uav_buffer, sizeof(u32), (64ull * 1024) / sizeof(u32));

	readback_uav_memory = device->allocate_memory(MemoryType::Readback, MemorySubType::Buffer, 64ull * 1024);
	readback_uav_buffer = device->create_buffer(readback_uav_memory, 64ull * 1024, BufferType::GeneralDataBuffer, false);

	// Defaults
	model_data.vertex_shading_mod = 1.0f;
	model_data.pixel_order_data1.x = 1.0f;
	model_data.pixel_order_data1.y = 1.0f/3.0f;

}

void ModelViewer::Update(float delta_time, float total_time, const Camera& camera, DebugDraw& debug_draw)
{
	bool recreate_pipeline = false;

	ImGui::Begin("Model Viewer", nullptr, 0);

	struct UAVReadBack
	{
		u32 UAV_INDEX_PIXELS_SHADED;
		u32 UAV_INDEX_MESH_SHADER_INVOCATIONS;
		u32 UAV_INDEX_MESH_SHADER_CULL_CONE_COUNT;
		u32 UAV_INDEX_MESH_SHADER_PRIM_COUNT;
		u32 UAV_INDEX_VERTEX_SHADER_INVOCATIONS;
		u32 UAV_INDEX_MESH_SHADER_CULL_SPHERE_COUNT;
		u32 UAV_INDEX_WAVE_INTRINSIC_COUNTER;
		u32 UAV_INDEX_AMPLIFICATION_SHADER_INVOCATIONS;
	} uav_readback_values = {};
	readback_uav_buffer->read_memory(0, &uav_readback_values, sizeof(uav_readback_values));

	// Model scale and shading etc
	if (ImGui::CollapsingHeader("Model"))
	{
		int list_changed = model_file_list_current;
		if (ImGui::BeginCombo("File", model_file_list[model_file_list_current].c_str()))
		{
			for (int n = 0; n < model_file_list.size(); n++)
			{
				const bool is_selected = (model_file_list_current == n);
				if (ImGui::Selectable(model_file_list[n].c_str(), is_selected))
					model_file_list_current = n;

				// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (model_file_list_current != list_changed) 
		{
			model_init_data.file_path = model_file_list[model_file_list_current];
			recreate_model = true;
		}
		ImGui::SeparatorText("Model Settings");
		ImGui::BeginDisabled(!device->SupportsMeshShaders());
		ImGui::Checkbox("Render with Mesh Shader", &render_as_mesh_shader);
		ImGui::EndDisabled();
		ImGui::BeginDisabled(scale_model_to_1);
		ImGui::SliderFloat("Render Scale", &model_scale, 0.0f, 20.0f);
		ImGui::EndDisabled();
		ImGui::SliderFloat("Render Percent", &render_percentage, 0.0f, 1.0f);
		if (ImGui::Checkbox("Scale model size -1/+1", &scale_model_to_1))
		{
			if (scale_model_to_1)
			{
				scale_mode_to_1_previous = model_scale;
			}
			else
			{
				model_scale = scale_mode_to_1_previous;
			}
		}
		ImGui::Checkbox("Rotate Model", &rotate_model);
		ImGui::SeparatorText("Model Info:");
		{
			ImGui::Text("Bounds: %3.3f, %3.3f, %3.3f", model->max_extent.x, model->max_extent.y, model->max_extent.z);
			ImGui::Text("Bounding Box Min: %3.3f, %3.3f, %3.3f", model->bounding_box_min.x, model->bounding_box_min.y, model->bounding_box_min.z);
			ImGui::Text("Bounding Box Max: %3.3f, %3.3f, %3.3f", model->bounding_box_max.x, model->bounding_box_max.y, model->bounding_box_max.z);

			size_t model_info_idx = 0;
			for (Model::MeshPart& mesh_part : model->GetMeshParts())
			{
				ImGui::PushID("ModelInfo");
				ImGui::PushID(model_info_idx);
				ImGui::SeparatorText("Mesh Part:"); ImGui::SameLine(); ImGui::Text("%i", model_info_idx + 1);
				ImGui::Checkbox("Render", render_model_bool_array + model_info_idx);
				ImGui::Text("Triangles: %u", mesh_part.draw_count / 3);
				ImGui::Text("Vertex Count: %u", mesh_part.vertex_count);
				ImGui::Text("Vertex Cache Miss (ACMR): %0.2f", mesh_part.vertex_cache_miss_acmr);
				ImGui::Text("Vertex Cache Miss (ATVR): %0.2f", mesh_part.vertex_cache_miss_atvr);
				ImGui::Text("Bounds: %3.3f, %3.3f, %3.3f", mesh_part.max_extent.x, mesh_part.max_extent.y, mesh_part.max_extent.z);
				ImGui::Text("Bounding Box Min: %3.3f, %3.3f, %3.3f", mesh_part.bounding_box_min.x, mesh_part.bounding_box_min.y, mesh_part.bounding_box_min.z);
				ImGui::Text("Bounding Box Max: %3.3f, %3.3f, %3.3f", mesh_part.bounding_box_max.x, mesh_part.bounding_box_max.y, mesh_part.bounding_box_max.z);

				ImGui::PopID();
				ImGui::PopID();
				model_info_idx++;
			}
		}
		
		ImGui::SeparatorText("UAV Readback:");
		{
			ImGui::PushID("UAV ReadbackInfo");
			ImGui::Text("Pixels Shaded:   %u", uav_readback_values.UAV_INDEX_PIXELS_SHADED);
			ImGui::Text("VS Invocations:  %u", uav_readback_values.UAV_INDEX_VERTEX_SHADER_INVOCATIONS);
			ImGui::Text("MS Invocations:  %u", uav_readback_values.UAV_INDEX_MESH_SHADER_INVOCATIONS);
			ImGui::Text("Meshlet Prims:   %u", uav_readback_values.UAV_INDEX_MESH_SHADER_PRIM_COUNT);
			ImGui::Text("Meshlets Culled (By Cone Dir): %u", uav_readback_values.UAV_INDEX_MESH_SHADER_CULL_CONE_COUNT);
			ImGui::Text("Meshlets Culled (By Sphere Frustum): %u", uav_readback_values.UAV_INDEX_MESH_SHADER_CULL_SPHERE_COUNT);
			ImGui::Text("Wave Group Count: %u", uav_readback_values.UAV_INDEX_WAVE_INTRINSIC_COUNTER);
			ImGui::Text("Amplification Shader Invocations: %u", uav_readback_values.UAV_INDEX_AMPLIFICATION_SHADER_INVOCATIONS);
			ImGui::PopID();
		}

		ImGui::SeparatorText("Mesh Optimizer");
		ImGui::BeginGroup();
		recreate_model = ImGui::RadioButton("Default", (int*)&model_init_data.vertex_cache_opt_mode, (int) Model::InitData::VertexCachOptimisation::Default) || recreate_model;
		recreate_model = ImGui::RadioButton("MeshOpt", (int*)&model_init_data.vertex_cache_opt_mode, (int)Model::InitData::VertexCachOptimisation::MeshOpt) || recreate_model;
		recreate_model = ImGui::RadioButton("DXMesh", (int*)&model_init_data.vertex_cache_opt_mode, (int)Model::InitData::VertexCachOptimisation::DXMesh) || recreate_model;
		recreate_model = ImGui::RadioButton("DXMeshLRU", (int*)&model_init_data.vertex_cache_opt_mode, (int)Model::InitData::VertexCachOptimisation::DXMeshLRU) || recreate_model;

		if (model_init_data.vertex_cache_opt_mode == Model::InitData::VertexCachOptimisation::DXMeshLRU)
		{
			recreate_model = ImGui::SliderInt("LRU Size", &model_init_data.dxmesh_lru_size, 1, 64) || recreate_model;
		} 
		else if (model_init_data.vertex_cache_opt_mode == Model::InitData::VertexCachOptimisation::DXMesh)
		{
			recreate_model = ImGui::SliderInt("Vertex Cache Size", &model_init_data.dxmesh_vertex_cache_size, 1, 64) || recreate_model;
			recreate_model = ImGui::SliderInt("Restart Value", &model_init_data.dxmesh_restart, 1, 64) || recreate_model;

			if (model_init_data.dxmesh_restart > model_init_data.dxmesh_vertex_cache_size)
			{
				model_init_data.dxmesh_restart = model_init_data.dxmesh_vertex_cache_size;
			}

		}

		ImGui::EndGroup();
		ImGui::Text("Mesh Simplification (LOD)");
		recreate_model = ImGui::Checkbox("Simplify", &model_init_data.meshopt_simplification) || recreate_model;
		ImGui::BeginDisabled(!model_init_data.meshopt_simplification);
		recreate_model = ImGui::SliderFloat("Threshold", &model_init_data.meshopt_simplification_threshold, 0.0f, 1.0f) || recreate_model;
		recreate_model = ImGui::SliderFloat("Target Error", &model_init_data.meshopt_simplification_target_error, 0.0f, 1.0f) || recreate_model;
		//ImGui::Text("Reported LOD Error: %f", )
		ImGui::EndDisabled();

		ImGui::SeparatorText("Mesh Shader");
		ImGui::BeginDisabled(!render_as_mesh_shader);
		ImGui::Checkbox("Amplification Shader Stage", &amplification_mesh_shader);
		ImGui::Checkbox("Cone Culling", &mesh_shader_cone_culling);
		ImGui::Checkbox("Sphere Frustum Culling", &mesh_shader_sphere_frustum_culling);

		ImGui::EndDisabled();

		ImGui::SeparatorText("Render Mode");
		ImGui::BeginDisabled(render_as_mesh_shader);
		ImGui::RadioButton("3D Model", (int*)&render_geo, (int)RenderGeo::Model);
		ImGui::RadioButton("Fullscreen Triangle", (int*)&render_geo, (int)RenderGeo::FullscreenTriangle);
		ImGui::RadioButton("Fullscreen Quad", (int*)&render_geo, (int)RenderGeo::FullscreenQuad);
		ImGui::EndDisabled();

		ImGui::Text("Render Mode:");
		ImGui::PushID("Render Mode Radio Buttons");
		ImGui::RadioButton("Default", (int*)&render_mode, 0);
		ImGui::RadioButton("Primitive Order", (int*)&render_mode, 1);
		ImGui::RadioButton("Vertex Order", (int*)&render_mode, 2);
		ImGui::RadioButton("Pixel Order", (int*)&render_mode, 3);
		ImGui::RadioButton("Meshlet Order", (int*)&render_mode, 4);
		ImGui::RadioButton("Meshlet Cull Angle", (int*)&render_mode, 5);
		ImGui::RadioButton("Wave Intrinsics", (int*)&render_mode, 6);
		ImGui::RadioButton("Amplification Order", (int*)&render_mode, 7);
		ImGui::PopID();

		if (render_mode == RenderMode::MeshletOrder || render_mode == RenderMode::MeshletCullAngle)
		{

		}
		else if (render_mode == RenderMode::VertexOrder || render_mode == RenderMode::PrimitiveOrder)
		{
			ImGui::PushID("Vert/Prim Shading ID IMGUI");
			if (ImGui::CollapsingHeader("Vert/Prim Shading"))
			{
				ImGui::SliderFloat("Modulus", &model_data.vertex_shading_mod, 0.0f, 1.0f);
			}
			ImGui::PopID();
		}
		else if (render_mode == RenderMode::PixelOrder)
		{
			const float pixel_to_shade_maximum = camera.GetCameraShaderData().screen_dimensions_and_depth_info.x * camera.GetCameraShaderData().screen_dimensions_and_depth_info.y;
			ImGui::PushID("Pixel Order ID IMGUI");
			if (ImGui::CollapsingHeader("Pixel Order"))
			{
				ImGui::SliderFloat("Pixel Order Scale", &model_data.pixel_order_data1.x, 0.0f, 4.0f);
				ImGui::SliderFloat("Modulus", &model_data.pixel_order_data1.y, 0.0f, 1.0f);
				ImGui::Checkbox("Coloured", &pixel_shade_order_coloured);

				ImGui::Checkbox("Show Range Over Time", &pixel_shade_order_ranged_colour);
				ImGui::Checkbox("Automated Over Time", &pixel_shade_order_automatic);
				ImGui::SliderFloat("Pixel To Show", &pixel_shade_order_pixel_to_shade, 0.0f, pixel_to_shade_maximum);
				ImGui::SliderFloat("Range Around Pixel", &pixel_shade_order_range, 0.0f, pixel_to_shade_maximum);
				ImGui::SliderFloat("Automated Speed", &pixel_shade_order_automated_speed, 0.0f, pixel_to_shade_maximum);
			}
			ImGui::PopID();

			if (pixel_shade_order_automatic)
			{
				pixel_shade_order_pixel_to_shade += (delta_time * pixel_shade_order_automated_speed);
				if (pixel_shade_order_pixel_to_shade > pixel_to_shade_maximum)
					pixel_shade_order_pixel_to_shade = 0.0f;
			}

			if (pixel_shade_order_ranged_colour) pixel_shade_order_coloured = false;
			
			//cb_data.scale[0] = scale;
			//cb_data.scale[1] = mod;
			model_data.pixel_order_data1.z = pixel_shade_order_coloured ? 1.0f : 0.0f;
			model_data.pixel_order_data1.w = pixel_shade_order_ranged_colour ? 1.0f : 0.0f;

			model_data.pixel_order_data2.x = pixel_shade_order_pixel_to_shade;
			model_data.pixel_order_data2.y = pixel_shade_order_range;
		}
		else if (render_mode == RenderMode::WaveIntrinsics)
		{
			ImGui::PushID("Wave Intrinsics ID IMGUI");
			if (ImGui::CollapsingHeader("Wave Intrinsics Order"))
			{
				ImGui::PushID("Wave Intrinsics Mode Radio Buttons");
				ImGui::RadioButton("Lane Indices", (int*)&wave_intrinsic_render_mode, 0);
				ImGui::RadioButton("Lane Order", (int*)&wave_intrinsic_render_mode, 1);
				ImGui::RadioButton("Wave Usage Ratio", (int*)&wave_intrinsic_render_mode, 2);
				ImGui::PopID();
			}
			ImGui::PopID();
			model_data.wave_intrinsics.x = (int)device->GetWaveLaneCountMax();
			model_data.wave_intrinsics.y = static_cast<int>(wave_intrinsic_render_mode);
		}
	}

	// Model scale
	if (scale_model_to_1)
	{
		float position_multiplier = 1.0f / std::max(std::max(model->max_extent.x, model->max_extent.y), model->max_extent.z);
		model_scale = position_multiplier;
	}

	// Mesh opt etc

	// Pipeline state
	if (ImGui::CollapsingHeader("Pipeline"))
	{
		recreate_pipeline |= ImGui::Checkbox("Wireframe", &render_wireframe);
		recreate_pipeline |= ImGui::Checkbox("Depth Enable", &depth_enable);
		recreate_pipeline |= ImGui::Checkbox("Depth Write", &depth_write);
		recreate_pipeline |= ImGui::Combo("Cull Mode", &cull_mode, "Back\0Front\0None\0", 3);
	}

	if (ImGui::CollapsingHeader("CPU Culling"))
	{
		ImGui::Checkbox("Accurate Culling", &cpu_culling.accurate_cull_check);
		ImGui::Checkbox("Cull All", &cpu_culling.cull_all);
		ImGui::Checkbox("Cull None", &cpu_culling.cull_none);
		ImGui::InputInt("Passed", &cpu_culling.stat_passed, 1, 100, ImGuiInputTextFlags_ReadOnly);
		ImGui::InputInt("Failed", &cpu_culling.stat_failed, 1, 100, ImGuiInputTextFlags_ReadOnly);
		cpu_culling.stat_passed	= 0;
		cpu_culling.stat_failed	= 0;
	}
	
	ImGui::End();

	// Model constant buffer
	model_data.model_matrix = DirectX::XMMatrixScaling(model_scale, model_scale, model_scale);
	model_data.time_frame_delta = delta_time;
	model_data.time_total = total_time;
	model_data.shading_mode = (int)render_mode;
	model_data.meshlet_culling.x = mesh_shader_cone_culling ? 1 : 0;
	model_data.meshlet_culling.y = mesh_shader_sphere_frustum_culling ? 1 : 0;
	model_data.meshlet_culling.z = 0;
	model_data.meshlet_culling.w = 0;

	if (rotate_model)
	{
		rotate_value += delta_time * 35.0f;
		rotate_value = fmodf(rotate_value, 360.0f);
		DirectX::XMMATRIX rotation_matrix = DirectX::XMMatrixRotationRollPitchYaw(0.0f, DirectX::XMConvertToRadians(rotate_value), 0.0f);
		model_data.model_matrix = DirectX::XMMatrixMultiply(model_data.model_matrix, rotation_matrix);
	}

	model_data.model_matrix_inverse = DirectX::XMMatrixInverse(nullptr, model_data.model_matrix);

	if (recreate_pipeline)
	{ 
		CreatePipeline();
	}

}

void ModelViewer::Render(CommandList& command_list, const Camera& camera, ConstantBufferView& cbv_camera, Ptr<UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw)
{
	if (recreate_model)
	{
		CreateModel(upload_heap);
		recreate_model = false;
	}

	if (model)
	{
		if (render_as_mesh_shader)
		{
			command_list.set_pipeline(amplification_mesh_shader ? amplification_mesh_shading.pipeline.get() : mesh_shading.pipeline.get());
			Binding b;
			b.cbv_binding_count = 2;
			b.set_cbv(cbv_camera, 0);
			b.uav_binding_count = 1;
			b.set_uav(uav, 0);
			b.srv_binding_count = 5;
			b.set_srv(model->GetVertexBufferSRV(), 0);
			int render_idx = 0;
			command_list.clear_buffer_uint(uav, srv, 0);

			for (Model::MeshPart& mesh_part : model->GetMeshParts())
			{
				if (!MeshPartVisible(camera, DirectX::XMFLOAT3(), mesh_part))
					continue;

				//debug_draw->DrawAABB(DebugDraw::ColourRGBA(), {}, mesh_part.aabb);

				b.set_srv(mesh_part.mesh_shader_data.gpu_meshlets_view_srv, 1);
				b.set_srv(mesh_part.mesh_shader_data.gpu_unique_vertex_indices_view_srv, 2);
				b.set_srv(mesh_part.mesh_shader_data.gpu_primitive_indices_view_srv, 3);
				b.set_srv(mesh_part.mesh_shader_data.gpu_culldata_view_srv, 4); 

				model_data.meshlet_count = mesh_part.mesh_shader_data.meshlets.size();
				model_data.primitive_count = mesh_part.draw_count / 3;
				model_data.vertex_count = mesh_part.vertex_count;
				model_data.meshlet_vb_offset = mesh_part.vb_offset;
				sg::ConstantBufferView cbv_model = cbuffer.AllocateAndWrite(model_data); 
				b.set_cbv(cbv_model, 1);

				command_list.bind(b, PipelineType::Geometry);

				if (render_model_bool_array[render_idx])
				{
					u32 dispatch_value;
					if (amplification_mesh_shader)
					{
						static constexpr u32 AMPLIFICATION_GROUP_SIZE = 32; // This must match what is used in shader. Todo: move to shared header
						dispatch_value = DivRoundUp(mesh_part.mesh_shader_data.meshlets.size(), AMPLIFICATION_GROUP_SIZE);
					}
					else
					{
						dispatch_value = static_cast<sg::u32>(mesh_part.mesh_shader_data.meshlets.size() * render_percentage);
					}

					if (dispatch_value > 0)
					{
						command_list.dispatch_mesh(dispatch_value);
					}
				}
				render_idx++;

				for (const meshopt_Bounds& meshlet_bounds :  mesh_part.mesh_shader_data.meshlet_bounds)
				{
					DirectX::XMFLOAT3 pos;
					DirectX::XMStoreFloat3(&pos, DirectX::XMVector3Transform(DirectX::XMLoadFloat3((DirectX::XMFLOAT3*)meshlet_bounds.center), model_data.model_matrix));
					float diameter = meshlet_bounds.radius * 2.0f * model_scale;
					debug_draw.DrawSphere(DebugDraw::ColourRGBA(255,0,0), pos, diameter, 9);
				}
			}
		}
		else
		{
			command_list.set_pipeline(pipeline.get());

			Binding b;
			b.cbv_binding_count = 2;
			b.set_cbv(cbv_camera, 0);
			b.uav_binding_count = 1;
			b.set_uav(uav, 0);

			command_list.clear_buffer_uint(uav, srv, 0);

			command_list.bind_vertex_buffer(model->GetVertexBufferView());
			command_list.bind_index_buffer(model->GetIndexBufferView());

			int render_idx = 0;
			for (Model::MeshPart& mesh_part : model->GetMeshParts())
			{
				if (!MeshPartVisible(camera, DirectX::XMFLOAT3(), mesh_part))
					continue;

				//debug_draw->DrawAABB(DebugDraw::ColourRGBA(), {}, mesh_part.aabb);

				model_data.primitive_count = mesh_part.draw_count / 3;
				model_data.vertex_count = mesh_part.vertex_count;
				sg::ConstantBufferView cbv_model = cbuffer.AllocateAndWrite(model_data);
				b.set_cbv(cbv_model, 1);
				command_list.bind(b, PipelineType::Geometry);

				if (render_geo == RenderGeo::FullscreenTriangle || render_geo == RenderGeo::FullscreenQuad)
				{
					command_list.set_pipeline((render_geo == RenderGeo::FullscreenTriangle) ? pipeline_fullscreen_triangle.get() : pipeline_fullscreen_quad.get());
					command_list.draw_instanced((render_geo == RenderGeo::FullscreenTriangle) ? 3 : 6, 1, 0, 0);
					break;
				}

				if (render_model_bool_array[render_idx])
				{
					command_list.draw_indexed_instanced(static_cast<sg::u32>(mesh_part.draw_count * render_percentage), 1, mesh_part.ib_offset, mesh_part.vb_offset, 0);
				}
				render_idx++;
			}
		}

		// Copy UAV to readback buffer.
		command_list.copy_buffer_to_buffer(readback_uav_buffer.get(), uav_buffer.get());
	}
}

void ModelViewer::CreatePipeline()
{
	pipeline_desc.input_layout = Model::Vertex::make_input_layout();
	pipeline_desc.vertex_shader = shader_vertex;
	pipeline_desc.pixel_shader = shader_pixel;
	pipeline_desc.render_target_count = 1;
	pipeline_desc.render_target_format_list[0] = render_target_format;
	pipeline_desc.depth_stencil_format = depth_stencil_format;
	pipeline_desc.depth_stencil_desc.depth_enable = depth_enable;
	pipeline_desc.depth_stencil_desc.depth_write = depth_write;
	pipeline_desc.rasterizer_desc.fill_mode = render_wireframe ? Rasterizer::FillMode::Wireframe : Rasterizer::FillMode::Solid;
	const Rasterizer::CullMode cull_values[] = { Rasterizer::CullMode::Back, Rasterizer::CullMode::Front, Rasterizer::CullMode::None };
	pipeline_desc.rasterizer_desc.cull_mode = cull_values[cull_mode];
	pipeline = device->create_pipeline(pipeline_desc, pipeline_binding_desc);
	seAssert(pipeline != nullptr, "Failed to create model view pipeline");

	pipeline_desc.input_layout = {};

	pipeline_desc.vertex_shader = shader_vertex_triangle;
	pipeline_fullscreen_triangle = device->create_pipeline(pipeline_desc, pipeline_binding_desc);
	seAssert(pipeline_fullscreen_triangle != nullptr, "Failed to create model view pipeline");

	pipeline_desc.vertex_shader = shader_vertex_quad;
	pipeline_fullscreen_quad = device->create_pipeline(pipeline_desc, pipeline_binding_desc);
	seAssert(pipeline_fullscreen_quad != nullptr, "Failed to create model view pipeline");

	// Mesh shader pipeline
	{
		mesh_shading.pipeline_desc.mesh_shader = mesh_shading.shader_mesh;
		mesh_shading.pipeline_desc.pixel_shader = mesh_shading.shader_pixel;
		mesh_shading.pipeline_desc.render_target_count = 1;
		mesh_shading.pipeline_desc.render_target_format_list[0] = render_target_format;
		mesh_shading.pipeline_desc.depth_stencil_format = depth_stencil_format;
		mesh_shading.pipeline_desc.depth_stencil_desc.depth_enable = depth_enable;
		mesh_shading.pipeline_desc.depth_stencil_desc.depth_write = depth_write;
		mesh_shading.pipeline_desc.rasterizer_desc.fill_mode = render_wireframe ? Rasterizer::FillMode::Wireframe : Rasterizer::FillMode::Solid;
		const Rasterizer::CullMode cull_values[] = { Rasterizer::CullMode::Back, Rasterizer::CullMode::Front, Rasterizer::CullMode::None };
		mesh_shading.pipeline_desc.rasterizer_desc.cull_mode = cull_values[cull_mode];
		mesh_shading.pipeline = device->create_pipeline(mesh_shading.pipeline_desc, mesh_shading.binding_desc);
	}

	// Amplification & Mesh shader pipeline
	{
		amplification_mesh_shading.pipeline_desc.amp_shader = amplification_mesh_shading.shader_amplification;
		amplification_mesh_shading.pipeline_desc.mesh_shader = amplification_mesh_shading.shader_mesh;
		amplification_mesh_shading.pipeline_desc.pixel_shader = amplification_mesh_shading.shader_pixel;
		amplification_mesh_shading.pipeline_desc.render_target_count = 1;
		amplification_mesh_shading.pipeline_desc.render_target_format_list[0] = render_target_format;
		amplification_mesh_shading.pipeline_desc.depth_stencil_format = depth_stencil_format;
		amplification_mesh_shading.pipeline_desc.depth_stencil_desc.depth_enable = depth_enable;
		amplification_mesh_shading.pipeline_desc.depth_stencil_desc.depth_write = depth_write;
		amplification_mesh_shading.pipeline_desc.rasterizer_desc.fill_mode = render_wireframe ? Rasterizer::FillMode::Wireframe : Rasterizer::FillMode::Solid;
		const Rasterizer::CullMode cull_values[] = { Rasterizer::CullMode::Back, Rasterizer::CullMode::Front, Rasterizer::CullMode::None };
		amplification_mesh_shading.pipeline_desc.rasterizer_desc.cull_mode = cull_values[cull_mode];
		amplification_mesh_shading.pipeline = device->create_pipeline(amplification_mesh_shading.pipeline_desc, amplification_mesh_shading.binding_desc);
	}

}

void ModelViewer::CreateModel(Ptr<UploadHeap>& upload_heap)
{
	if (render_model_bool_array)
	{
		delete render_model_bool_array;
		render_model_bool_array = nullptr;
	}

	model = Ptr<Model>(new Model(device.get(), upload_heap.get(), model_init_data));

	if (model)
	{
		render_model_bool_array = new bool[model->GetMeshParts().size()];
		for (size_t i = 0; i < model->GetMeshParts().size(); i++)
		{
			render_model_bool_array[i] = true;
		}
	}
}

bool ModelViewer::MeshPartVisible(const Camera& camera, DirectX::XMFLOAT3 position, Model::MeshPart& mesh_part)
{
	float radius = std::max<float>(std::max<float>(fabsf(mesh_part.max_extent.x), fabsf(mesh_part.max_extent.y)), fabsf(mesh_part.max_extent.z));
	DirectX::BoundingSphere bs(position, radius);
	bool visible = false;

	if (cpu_culling.cull_all)
	{
		visible = false;
	}
	else if (cpu_culling.cull_none)
	{
		visible = true;
	}
	else
	{
		visible = cpu_culling.accurate_cull_check ? camera.IsInFrustum_Accurate(bs) : camera.IsInFrustum_Fast(bs);
	}

	if (visible)
	{
		cpu_culling.stat_passed++;
	}
	else
	{
		cpu_culling.stat_failed++;
	}
	return visible;
}
