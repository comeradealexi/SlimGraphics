#include "ModelViewer.h"
#include <sgPlatformInclude.h>
#include <imgui.h>
#include <seEngineBasicFileIO.h>
#include "Camera.h"
#include <algorithm>
#include "sgUtils.h"

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
	pipeline_binding_desc.srv_binding_count = 3;
	pipeline_binding_desc.uav_binding_count = 1;
	pipeline_binding_desc.sampler_binding_count = 3;

	mesh_shading.binding_desc = {};
	mesh_shading.binding_desc.cbv_binding_count = 2;
	mesh_shading.binding_desc.srv_binding_count = 8;
	mesh_shading.binding_desc.uav_binding_count = 1;
	mesh_shading.binding_desc.sampler_binding_count = 3;

	amplification_mesh_shading.binding_desc = {};
	amplification_mesh_shading.binding_desc.cbv_binding_count = 2;
	amplification_mesh_shading.binding_desc.srv_binding_count = 8;
	amplification_mesh_shading.binding_desc.uav_binding_count = 1;
	amplification_mesh_shading.binding_desc.sampler_binding_count = 3;

	{
		shader_vertex = create_vertex_shader(*device, "ShaderBin_Debug\\ModelViewer_VertexShader.PC_DXC");
		shader_vertex_simple = create_vertex_shader(*device, "ShaderBin_Debug\\SimplifiedModelViewer_VertexShader.PC_DXC");

		shaders_pixel[(int)StandardPixelPipelineMode::Full] = create_pixel_shader(*device, "ShaderBin_Debug\\ModelViewer_PixelShader.PC_DXC");
		shaders_pixel_eds[(int)StandardPixelPipelineMode::Full] = create_pixel_shader(*device, "ShaderBin_Debug\\ModelViewer_PixelShader_EDS.PC_DXC");

		shaders_pixel[(int)StandardPixelPipelineMode::Simple] = create_pixel_shader(*device, "ShaderBin_Debug\\SimplifiedModelViewer_PixelShader.PC_DXC");
		shaders_pixel_eds[(int)StandardPixelPipelineMode::Simple] = create_pixel_shader(*device, "ShaderBin_Debug\\SimplifiedModelViewer_PixelShader_EDS.PC_DXC");

		shaders_pixel[(int)StandardPixelPipelineMode::SimpleAndDiscard] = create_pixel_shader(*device, "ShaderBin_Debug\\SimplifiedModelViewer_PixelShader_DISCARD.PC_DXC");
		shaders_pixel_eds[(int)StandardPixelPipelineMode::SimpleAndDiscard] = create_pixel_shader(*device, "ShaderBin_Debug\\SimplifiedModelViewer_PixelShader_DISCARD_EDS.PC_DXC");

		shader_vertex_middle_triangle = create_vertex_shader(*device, "ShaderBin_Debug\\ModelViewer_VertexShaderMiddleTriangle.PC_DXC");
		shader_vertex_triangle = create_vertex_shader(*device, "ShaderBin_Debug\\ModelViewer_VertexShaderTriangle.PC_DXC");
		shader_vertex_quad = create_vertex_shader(*device, "ShaderBin_Debug\\ModelViewer_VertexShaderQuad.PC_DXC");
	}

	// Mesh Shader
	{
		mesh_shading.shader_mesh = create_mesh_shader(*device, "ShaderBin_Debug\\ModelViewer_MeshShader.PC_DXC");
		mesh_shading.shader_pixel = create_pixel_shader(*device, "ShaderBin_Debug\\ModelViewer_PixelMeshShader.PC_DXC");
		mesh_shading.shader_pixel_eds = create_pixel_shader(*device, "ShaderBin_Debug\\ModelViewer_PixelMeshShader_EDS.PC_DXC");
	}

	// Amplification + Mesh Shader
	{
		amplification_mesh_shading.shader_amplification = create_amplification_shader(*device, "ShaderBin_Debug\\ModelViewer_MeshletAmplificationAS.PC_DXC");
		amplification_mesh_shading.shader_mesh = create_mesh_shader(*device, "ShaderBin_Debug\\ModelViewer_MeshletAmplificationMS.PC_DXC");
		amplification_mesh_shading.shader_pixel = create_pixel_shader(*device, "ShaderBin_Debug\\ModelViewer_MeshletAmplificationPS.PC_DXC");
		amplification_mesh_shading.shader_pixel_eds = create_pixel_shader(*device, "ShaderBin_Debug\\ModelViewer_MeshletAmplificationPS_EDS.PC_DXC");
	}

	CreatePipeline();

	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/lpshead/head.OBJ");
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

	model_file_list.push_back("../SlimGraphicsAssets/SponzaDae/sponza.dae");
	model_file_list.push_back("../SlimGraphicsAssets/SponzaGL/sponza.obj");


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

	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/CGTree/cgaxis_models_115_37_obj.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/buddha/buddha.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/CornellBox/CornellBox-Mirror.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/CornellBox/CornellBox-Original.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/CornellBox/CornellBox-Sphere.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/CornellBox/CornellBox-Water.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/CornellBox/CornellBox-Empty-CO.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/CornellBox/CornellBox-Empty-RG.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/CornellBox/CornellBox-Empty-Squashed.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/CornellBox/CornellBox-Empty-White.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/CornellBox/CornellBox-Glossy.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/CornellBox/CornellBox-Glossy-Floor.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/erato/erato.obj");
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/mori_knob/testObj.obj");	
	model_file_list.push_back("../SlimGraphicsAssets/CGArchive/Nefertiti.obj");	

	// Speed Tree
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/White Oak/HighPoly/White_Oak.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/European Linden/HighPoly/European_Linden.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/Azalea/HighPoly/Azalea.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/Hedge/HighPoly/Hedge.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/Japanese Maple/HighPoly/Japanese_Maple.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/Red Maple Young/HighPoly/Red_Maple_Young.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/Azalea/LowPoly/Azalea_LowPoly.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/Boston Fern/HighPoly/Boston_Fern.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/Hedge/LowPoly/Hedge_LowPoly.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/European Linden/LowPoly/European_Linden_LowPoly.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/Japanese Maple/LowPoly/Japanese_Maple_LowPoly.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/Red Maple Young/LowPoly/Red_Maple_Young_LowPoly.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/White Oak/LowPoly/White_Oak_LowPoly.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/Backyard Grass/HighPoly/Backyard_Grass.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/Boston Fern/LowPoly/Boston_Fern_LowPoly.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/SpeedTree/Backyard Grass/LowPoly/Backyard_Grass_LowPoly.fbx");

	// https://developer.nvidia.com/orca

	model_file_list.push_back("../SlimGraphicsAssets/Bistro_v5_2/BistroInterior.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/Bistro_v5_2/BistroInterior_Wine.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/Bistro_v5_2/BistroExterior.fbx");

	model_file_list.push_back("../SlimGraphicsAssets/Hermanubis/Hermanubis_High.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/Hermanubis/Hermanubis_low.fbx");

	model_file_list.push_back("../SlimGraphicsAssets/UE4SunTemple_v4/SunTemple/SunTemple.fbx");

	model_file_list.push_back("../SlimGraphicsAssets/EmeraldSquare_v4_1/EmeraldSquare_Day.fbx");
	model_file_list.push_back("../SlimGraphicsAssets/EmeraldSquare_v4_1/EmeraldSquare_Dusk.fbx");

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

// https://zeux.io/2023/01/12/approximate-projected-bounds/
bool projectBox(DirectX::XMFLOAT3 bmin, DirectX::XMFLOAT3 bmax, float znear, DirectX::XMMATRIX viewProjection, DirectX::XMFLOAT4& aabb)
{
	using namespace DirectX;
	XMFLOAT4 P[8];

	XMStoreFloat4( &P[0], XMVector4Transform(XMVectorSet(bmin.x, bmin.y, bmin.z, 1.0), viewProjection));
	XMStoreFloat4( &P[1], XMVector4Transform(XMVectorSet(bmin.x, bmin.y, bmax.z, 1.0), viewProjection));
	XMStoreFloat4( &P[2], XMVector4Transform(XMVectorSet(bmin.x, bmax.y, bmin.z, 1.0), viewProjection));
	XMStoreFloat4( &P[3], XMVector4Transform(XMVectorSet(bmin.x, bmax.y, bmax.z, 1.0), viewProjection));
	XMStoreFloat4( &P[4], XMVector4Transform(XMVectorSet(bmax.x, bmin.y, bmin.z, 1.0), viewProjection));
	XMStoreFloat4( &P[5], XMVector4Transform(XMVectorSet(bmax.x, bmin.y, bmax.z, 1.0), viewProjection));
	XMStoreFloat4( &P[6], XMVector4Transform(XMVectorSet(bmax.x, bmax.y, bmin.z, 1.0), viewProjection));
	XMStoreFloat4( &P[7], XMVector4Transform(XMVectorSet(bmax.x, bmax.y, bmax.z, 1.0), viewProjection));

	aabb = {};

	float min_w = P[0].w;
	//if (min(P0.w, P1.w, P2.w, P3.w, P4.w, P5.w, P6.w, P7.w) < znear) return false;
	for (size_t i = 1; i < 8; i++)
	{
		min_w = std::max(min_w, P[i].w);
	}
	if (min_w < znear) return false;

	aabb.x = P[0].x / P[0].w;
	aabb.y = P[0].y / P[0].w;
	aabb.z = P[0].x / P[0].w;
	aabb.w = P[0].y / P[0].w;

	for (size_t i = 1; i < 8; i++)
	{
		aabb.x = std::min(aabb.x, P[i].x / P[i].w);
		aabb.y = std::min(aabb.y, P[i].y / P[i].w);

		aabb.z = std::max(aabb.z, P[i].x / P[i].w);
		aabb.w = std::max(aabb.w, P[i].y / P[i].w);
	}

	//aabb.xy = min(
	//	P0.xy / P0.w, P1.xy / P1.w, P2.xy / P2.w, P3.xy / P3.w,
	//	P4.xy / P4.w, P5.xy / P5.w, P6.xy / P6.w, P7.xy / P7.w);
	//aabb.zw = max(
	//	P0.xy / P0.w, P1.xy / P1.w, P2.xy / P2.w, P3.xy / P3.w,
	//	P4.xy / P4.w, P5.xy / P5.w, P6.xy / P6.w, P7.xy / P7.w);

	// clip space -> uv space
	//aabb = aabb.xwzy * vec4(0.5f, -0.5f, 0.5f, -0.5f) + vec4(0.5f);

	return true;
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
		ImGui::Indent();
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
		ImGui::BeginDisabled(scale_model_to_1);
		ImGui::SliderFloat("Render Scale", &model_scale, 0.0f, 20.0f);
		ImGui::EndDisabled();
		ImGui::SliderFloat("Render Percent", &render_percentage, 0.0f, 1.0f);
		int max_models = model ? model->GetMeshParts().size() : 0;
		ImGui::SliderInt("Models To Render", &model_render_count, 0, max_models);
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
		ImGui::Checkbox("Auto Rotate Model", &auto_rotate_model);
		if (ImGui::Button("0 degrees")) { rotate_value = 0.0f; }
		ImGui::SameLine();
		if (ImGui::Button("90 degrees")) { rotate_value = 90.0f; }
		ImGui::SameLine();
		if (ImGui::Button("180 degrees")) { rotate_value = 180.0f; }
		ImGui::SameLine();
		if (ImGui::Button("270 degrees")) { rotate_value = 270.0f; }
		ImGui::SliderFloat("Rotation", &rotate_value, 0.0f, 360.0f);
		// Mesh part screen size
		if (model)
		{
			if (ImGui::CollapsingHeader("Model Size Info"))
			{
				ImGui::PushID("ModelInfo");
				uint32_t model_info_idx = 0;
				for (Model::MeshPart& mesh_part : model->GetMeshParts())
				{
					ImGui::PushID(model_info_idx++);
					using namespace DirectX;
					const float radius = std::max<float>(std::max<float>(fabsf(mesh_part.max_extent.x), fabsf(mesh_part.max_extent.y)), fabsf(mesh_part.max_extent.z));
					XMVECTOR centre = XMVectorSet(mesh_part.aabb.Center.x, mesh_part.aabb.Center.y, mesh_part.aabb.Center.z, 0.0f);
					const XMVECTOR screen_space = XMVector3Transform(centre, camera.GetCameraShaderData().view_projection_matrix);
					ImGui::Text("radius: %0.2f", radius);
					ImGui::Text("centre: %0.2f %0.2f %0.2f", centre.m128_f32[0], centre.m128_f32[1], centre.m128_f32[2]);
					ImGui::Text("screen_space: %0.2f %0.2f %0.2f", screen_space.m128_f32[0], screen_space.m128_f32[1], screen_space.m128_f32[2]);
					ImGui::Text("screen_space / w: %0.2f %0.2f %0.2f", screen_space.m128_f32[0] / screen_space.m128_f32[3], screen_space.m128_f32[1] / screen_space.m128_f32[3], screen_space.m128_f32[2] / screen_space.m128_f32[3]);

					XMFLOAT3 c;
					XMStoreFloat3(&c, centre);
					XMFLOAT4 outaabb;
					bool bMult = projectBox(mesh_part.bounding_box_min, mesh_part.bounding_box_max, camera.GetNearPlane(), camera.GetCameraShaderData().view_projection_matrix, outaabb);
					ImGui::BeginDisabled();
					ImGui::Checkbox("projectSphereView returned bool", &bMult);
					ImGui::EndDisabled();
					ImGui::Text("aabb: xmin: %0.4f ymin: %0.4f", outaabb.x, outaabb.y);
					ImGui::Text("aabb: xmax: %0.4f ymax: %0.4f", outaabb.z, outaabb.w);
					ImGui::Text("Screen Percent X: %0.2f Screen Percent Y: %0.2f", fabsf(std::max(outaabb.x, -1.0f) - std::min(outaabb.z, 1.0f)) * 50.0f, fabsf(std::max(outaabb.y, -1.0f) - std::min(outaabb.w, 1.0f)) * 50.0f);
					ImGui::PopID();
				}
				ImGui::PopID();
			}
		}
		ImGui::SeparatorText("Model Info:");
		{
			ImGui::Text("Bounds: %3.3f, %3.3f, %3.3f", model->max_extent.x, model->max_extent.y, model->max_extent.z);
			ImGui::Text("Bounding Box Min: %3.3f, %3.3f, %3.3f", model->bounding_box_min.x, model->bounding_box_min.y, model->bounding_box_min.z);
			ImGui::Text("Bounding Box Max: %3.3f, %3.3f, %3.3f", model->bounding_box_max.x, model->bounding_box_max.y, model->bounding_box_max.z);

			if (ImGui::CollapsingHeader("Model Parts"))
			{
				ImGui::Indent();
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
				ImGui::Unindent();
			}
		}
		ImGui::Unindent();
	}

	{
		if (ImGui::CollapsingHeader("UAV Readback"))
		{
			ImGui::PushID("UAV ReadbackInfo");
			ImGui::Text("Pixels Shaded:   %u", uav_readback_values.UAV_INDEX_PIXELS_SHADED);
			ImGui::Text("VS Invocations:  %u", uav_readback_values.UAV_INDEX_VERTEX_SHADER_INVOCATIONS);
			ImGui::Text("MS Invocations:  %u", uav_readback_values.UAV_INDEX_MESH_SHADER_INVOCATIONS);
			ImGui::Text("Meshlet Prims:   %u", uav_readback_values.UAV_INDEX_MESH_SHADER_PRIM_COUNT);
			ImGui::Text("Meshlets Culled (By Cone Dir): %u", uav_readback_values.UAV_INDEX_MESH_SHADER_CULL_CONE_COUNT);
			ImGui::Text("Meshlets Culled (By Sphere Frustum): %u", uav_readback_values.UAV_INDEX_MESH_SHADER_CULL_SPHERE_COUNT);
			ImGui::Text("%s: %u", wave_intrinsic_render_mode == WaveIntrinsicRenderMode::HelperLaneViewer ? "Non Helper Pixel Count" : "Wave Group Count", uav_readback_values.UAV_INDEX_WAVE_INTRINSIC_COUNTER);
			ImGui::Text("Amplification Shader Invocations: %u", uav_readback_values.UAV_INDEX_AMPLIFICATION_SHADER_INVOCATIONS);
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("Mesh Optimizer"))
		{
			ImGui::BeginGroup();
			recreate_model = ImGui::RadioButton("Default", (int*)&model_init_data.vertex_cache_opt_mode, (int)Model::InitData::VertexCachOptimisation::Default) || recreate_model;
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
		}

		if (ImGui::CollapsingHeader("Render Settings"))
		{
			ImGui::SeparatorText("Model Settings");
			ImGui::BeginDisabled(!device->SupportsMeshShaders());
			ImGui::Checkbox("Render with Mesh Shader", &render_as_mesh_shader);
			ImGui::EndDisabled();
			ImGui::Checkbox("Force Early Depth Stencil", &use_eds);
			ImGui::SeparatorText("Mesh Shader");
			ImGui::BeginDisabled(!render_as_mesh_shader);
			ImGui::Checkbox("Amplification Shader Stage", &amplification_mesh_shader);
			ImGui::Checkbox("Cone Culling", &mesh_shader_cone_culling);
			ImGui::Checkbox("Sphere Frustum Culling", &mesh_shader_sphere_frustum_culling);

			ImGui::EndDisabled();

			ImGui::SeparatorText("Render Mode");
			ImGui::BeginDisabled(render_as_mesh_shader);
			ImGui::Text("Geometry:");
			ImGui::RadioButton("3D Model", (int*)&render_geo, (int)RenderGeo::Model);
			ImGui::RadioButton("Fullscreen Triangle", (int*)&render_geo, (int)RenderGeo::FullscreenTriangle);
			ImGui::RadioButton("Middle Triangle", (int*)&render_geo, (int)RenderGeo::MiddleTriangle);
			ImGui::RadioButton("Fullscreen Quad", (int*)&render_geo, (int)RenderGeo::FullscreenQuad);
			ImGui::Text("Shader:");
			{
				recreate_pipeline |= ImGui::RadioButton("Full", (int*)&standard_vsps_render_mode, (int)StandardPixelPipelineMode::Full);
				recreate_pipeline |= ImGui::RadioButton("Simple", (int*)&standard_vsps_render_mode, (int)StandardPixelPipelineMode::Simple);
				recreate_pipeline |= ImGui::RadioButton("Simple + Discard", (int*)&standard_vsps_render_mode, (int)StandardPixelPipelineMode::SimpleAndDiscard);
			}
			ImGui::SliderFloat("Discard Value:", &model_data.simplified_shading.x, 0.0f, 1.0f);
			ImGui::EndDisabled();

			ImGui::BeginDisabled(standard_vsps_render_mode != StandardPixelPipelineMode::Full);
			ImGui::Text("Advanced Render Mode:");
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
			ImGui::EndDisabled();

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
				ImGui::Indent();
				ImGui::PushID("Wave Intrinsics ID IMGUI");
				if (ImGui::CollapsingHeader("Wave Intrinsics Order"))
				{
					ImGui::PushID("Wave Intrinsics Mode Radio Buttons");
					ImGui::RadioButton("Lane Indices", (int*)&wave_intrinsic_render_mode, 0);
					ImGui::RadioButton("Lane Order", (int*)&wave_intrinsic_render_mode, 1);
					ImGui::RadioButton("Wave Usage Ratio", (int*)&wave_intrinsic_render_mode, 2);
					ImGui::RadioButton("Helper Lane", (int*)&wave_intrinsic_render_mode, 3);
					ImGui::RadioButton("Wave Count", (int*)&wave_intrinsic_render_mode, 4);
					ImGui::RadioButton("All Waves Same", (int*)&wave_intrinsic_render_mode, 5);
					ImGui::PopID();
				}
				ImGui::PopID();
				ImGui::Unindent();
				model_data.wave_intrinsics.x = (int)device->GetWaveLaneCountMax();
				model_data.wave_intrinsics.y = static_cast<int>(wave_intrinsic_render_mode);
			}
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

	if (ImGui::CollapsingHeader("CPU Optimisations"))
	{
		ImGui::SeparatorText("Culling");
		ImGui::Checkbox("Accurate Culling", &cpu_culling.accurate_cull_check);
		ImGui::Checkbox("Cull All", &cpu_culling.cull_all);
		ImGui::Checkbox("Cull None", &cpu_culling.cull_none);
		ImGui::InputInt("Passed", &cpu_culling.stat_passed, 1, 100, ImGuiInputTextFlags_ReadOnly);
		ImGui::InputInt("Failed", &cpu_culling.stat_failed, 1, 100, ImGuiInputTextFlags_ReadOnly);
		ImGui::SeparatorText("Sorting");
		ImGui::Checkbox("Sort", &cpu_sorting.sort);
		ImGui::BeginDisabled(!cpu_sorting.sort);
		ImGui::Checkbox("Front To Back", &cpu_sorting.front_to_back);
		ImGui::EndDisabled();

		cpu_culling.stat_passed	= 0;
		cpu_culling.stat_failed	= 0;
	}

	if (ImGui::CollapsingHeader("Model Debug Drawing"))
	{
		ImGui::PushID("Model Debug Draw");
		ImGui::Checkbox("Model Parts (AABB)", &debug_drawing.render_model_parts_aabb);
		ImGui::Checkbox("Model Parts (Sphere)", &debug_drawing.render_model_parts_sphere);
		ImGui::Checkbox("Meshlet Parts", &debug_drawing.render_meshlet_parts);
		ImGui::PopID();
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

	if (auto_rotate_model || rotate_value > 0.0f)
	{
		if (auto_rotate_model)
		{
			rotate_value += delta_time * 35.0f;
			rotate_value = fmodf(rotate_value, 360.0f);
		}
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
		std::vector<Model::MeshPart*> render_list_ordered;
		render_list_ordered.reserve(model->GetMeshParts().size());
		
		for (size_t i = 0; i < model->GetMeshParts().size(); i++)
		{
			if (!render_model_bool_array[i])
				continue;

			if (!MeshPartVisible(camera, DirectX::XMFLOAT3(), model->GetMeshParts().at(i)))
				continue;

			render_list_ordered.push_back(&(model->GetMeshParts().at(i)));
		}

		if (cpu_sorting.sort && render_list_ordered.size() > 1)
		{
			std::sort(render_list_ordered.begin(), render_list_ordered.end(), [&](Model::MeshPart* a, Model::MeshPart* b) -> bool
				{					
					DirectX::XMVECTOR a_pos = DirectX::XMVector3Transform(DirectX::XMLoadFloat3(&a->aabb.Center), model_data.model_matrix);
					float a_distance = fabsf(DirectX::XMVector3Length(DirectX::XMVectorSubtract(a_pos, DirectX::XMLoadFloat3A(&camera.GetPosition()))).m128_f32[0]);

					DirectX::XMVECTOR b_pos = DirectX::XMVector3Transform(DirectX::XMLoadFloat3(&b->aabb.Center), model_data.model_matrix);
					float b_distance = fabsf(DirectX::XMVector3Length(DirectX::XMVectorSubtract(b_pos, DirectX::XMLoadFloat3A(&camera.GetPosition()))).m128_f32[0]);

					return cpu_sorting.front_to_back ? a_distance < b_distance : a_distance > b_distance;
				});
		}

		if (render_list_ordered.size() > model_render_count)
		{
			render_list_ordered.resize(model_render_count);
		}

		if (render_as_mesh_shader)
		{
			MeshShaderRendering& mesh_shader_rendering = amplification_mesh_shader ? amplification_mesh_shading : mesh_shading;
			command_list.set_pipeline(use_eds ? mesh_shader_rendering.pipeline_eds.get() : mesh_shader_rendering.pipeline.get());

			Binding b;
			b.cbv_binding_count = 2;
			b.set_cbv(cbv_camera, 0);
			b.uav_binding_count = 1;
			b.set_uav(uav, 0);
			b.srv_binding_count = 8;
			b.sampler_binding_count = 3;

			b.set_srv(model->GetVertexBufferSRV(), 3);
			command_list.clear_buffer_uint(uav, srv, 0);

			for (Model::MeshPart* mesh_part_ptr : render_list_ordered)
			{
				Model::MeshPart& mesh_part = *mesh_part_ptr;
				Model::Material& mesh_material = model->GetMaterial(mesh_part_ptr->material_index);

				b.set_srv(mesh_material.srv_diffuse, 0);
				b.set_srv(mesh_material.srv_specular, 1);
				b.set_srv(mesh_material.srv_normal, 2);

				if (debug_drawing.render_model_parts_aabb)
				{
					debug_draw.DrawAABB(DebugDraw::ColourRGBA(), {}, mesh_part.aabb);
				}
				if (debug_drawing.render_model_parts_sphere)
				{
					float radius = std::max<float>(std::max<float>(fabsf(mesh_part.max_extent.x), fabsf(mesh_part.max_extent.y)), fabsf(mesh_part.max_extent.z));
					debug_draw.DrawSphere(DebugDraw::ColourRGBA(), mesh_part.aabb.Center, radius * 2.0, 8ui64);
				}

				b.set_srv(mesh_part.mesh_shader_data.gpu_meshlets_view_srv, 4);
				b.set_srv(mesh_part.mesh_shader_data.gpu_unique_vertex_indices_view_srv, 5);
				b.set_srv(mesh_part.mesh_shader_data.gpu_primitive_indices_view_srv, 6);
				b.set_srv(mesh_part.mesh_shader_data.gpu_culldata_view_srv, 7); 

				model_data.meshlet_count = mesh_part.mesh_shader_data.meshlets.size() * render_percentage;
				model_data.primitive_count = mesh_part.draw_count / 3;
				model_data.vertex_count = mesh_part.vertex_count;
				model_data.meshlet_vb_offset = mesh_part.vb_offset;
				sg::ConstantBufferView cbv_model = cbuffer.AllocateAndWrite(model_data); 
				b.set_cbv(cbv_model, 1);

				command_list.bind(b, PipelineType::Geometry);

				u32 dispatch_value;
				if (amplification_mesh_shader)
				{
					static constexpr u32 AMPLIFICATION_GROUP_SIZE = 32; // This must match what is used in shader. Todo: move to shared header
					dispatch_value = DivRoundUp(mesh_part.mesh_shader_data.meshlets.size() * render_percentage, AMPLIFICATION_GROUP_SIZE);
				}
				else
				{
					dispatch_value = static_cast<sg::u32>(mesh_part.mesh_shader_data.meshlets.size() * render_percentage);
				}

				if (dispatch_value > 0)
				{
					command_list.dispatch_mesh(dispatch_value);
				}

				if (debug_draw.IsEnabled() && debug_drawing.render_meshlet_parts)
				{
					for (const meshopt_Bounds& meshlet_bounds : mesh_part.mesh_shader_data.meshlet_bounds)
					{
						DirectX::XMFLOAT3 pos;
						DirectX::XMStoreFloat3(&pos, DirectX::XMVector3Transform(DirectX::XMLoadFloat3((DirectX::XMFLOAT3*)meshlet_bounds.center), model_data.model_matrix));
						float diameter = meshlet_bounds.radius * 2.0f * model_scale;
						debug_draw.DrawSphere(DebugDraw::ColourRGBA(255, 0, 0), pos, diameter, 9);
					}
				}
			}
		}
		else
		{
			command_list.set_pipeline(use_eds ? pipeline_eds.get() : pipeline.get());

			Binding b;
			b.cbv_binding_count = 2;
			b.set_cbv(cbv_camera, 0);
			b.uav_binding_count = 1;
			b.set_uav(uav, 0);
			b.srv_binding_count = 3;
			b.sampler_binding_count = 3;

			command_list.clear_buffer_uint(uav, srv, 0);

			command_list.bind_vertex_buffer(model->GetVertexBufferView());
			command_list.bind_index_buffer(model->GetIndexBufferView());

			int render_idx = 0;
			for (Model::MeshPart* mesh_part_ptr : render_list_ordered)
			{
				Model::MeshPart& mesh_part = *mesh_part_ptr;
				Model::Material& mesh_material = model->GetMaterial(mesh_part_ptr->material_index);

				b.set_srv(mesh_material.srv_diffuse, 0);
				b.set_srv(mesh_material.srv_specular, 1);
				b.set_srv(mesh_material.srv_normal, 2);

				if (debug_drawing.render_model_parts_aabb)
				{
					debug_draw.DrawAABB(DebugDraw::ColourRGBA(), {}, mesh_part.aabb);
				}
				if (debug_drawing.render_model_parts_sphere)
				{
					float radius = std::max<float>(std::max<float>(fabsf(mesh_part.max_extent.x), fabsf(mesh_part.max_extent.y)), fabsf(mesh_part.max_extent.z));
					debug_draw.DrawSphere(DebugDraw::ColourRGBA(), mesh_part.aabb.Center, radius * 2.0, 8ui64);
				}

				model_data.primitive_count = mesh_part.draw_count / 3;
				model_data.vertex_count = mesh_part.vertex_count;
				sg::ConstantBufferView cbv_model = cbuffer.AllocateAndWrite(model_data);
				b.set_cbv(cbv_model, 1);
				command_list.bind(b, PipelineType::Geometry);

				if (render_geo != RenderGeo::Model)
				{
					sg::Pipeline* pipeline = pipeline_fullscreen_triangle.get();
					if (render_geo == RenderGeo::MiddleTriangle)
					{
						pipeline = pipeline_middle_triangle.get();
					}
					else if (render_geo == RenderGeo::FullscreenQuad)
					{
						pipeline = pipeline_fullscreen_quad.get();
					}
					command_list.set_pipeline(pipeline);
					command_list.draw_instanced((render_geo == RenderGeo::FullscreenQuad) ? 6 : 3, 1, 0, 0);
					break;
				}

				command_list.draw_indexed_instanced(static_cast<sg::u32>(mesh_part.draw_count* render_percentage), 1, mesh_part.ib_offset, mesh_part.vb_offset, 0);
			}
		}

		// Copy UAV to readback buffer.
		command_list.copy_buffer_to_buffer(readback_uav_buffer.get(), uav_buffer.get());
	}
}

void ModelViewer::CreatePipeline()
{
	pipeline_desc.input_layout = Model::Vertex::make_input_layout();
	pipeline_desc.vertex_shader = standard_vsps_render_mode == StandardPixelPipelineMode::Full ? shader_vertex : shader_vertex_simple;
	pipeline_desc.pixel_shader = shaders_pixel[(int)standard_vsps_render_mode];
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
	
	// ROV Variant
	{
		pipeline_desc.pixel_shader = shaders_pixel_eds[(int)standard_vsps_render_mode];
		pipeline_eds = device->create_pipeline(pipeline_desc, pipeline_binding_desc);
		seAssert(pipeline_eds != nullptr, "Failed to create model view pipeline");

		// Restore original ps
		pipeline_desc.pixel_shader = shaders_pixel[(int)standard_vsps_render_mode];
	}

	pipeline_desc.input_layout = {};

	pipeline_desc.vertex_shader = shader_vertex_triangle;
	pipeline_fullscreen_triangle = device->create_pipeline(pipeline_desc, pipeline_binding_desc);
	seAssert(pipeline_fullscreen_triangle != nullptr, "Failed to create model view pipeline");

	pipeline_desc.vertex_shader = shader_vertex_middle_triangle;
	pipeline_middle_triangle = device->create_pipeline(pipeline_desc, pipeline_binding_desc);
	seAssert(pipeline_middle_triangle != nullptr, "Failed to create model view pipeline");

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

		mesh_shading.pipeline_desc.pixel_shader = mesh_shading.shader_pixel_eds;
		mesh_shading.pipeline_eds = device->create_pipeline(mesh_shading.pipeline_desc, mesh_shading.binding_desc);
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

		amplification_mesh_shading.pipeline_desc.pixel_shader = amplification_mesh_shading.shader_pixel_eds;
		amplification_mesh_shading.pipeline_eds = device->create_pipeline(amplification_mesh_shading.pipeline_desc, amplification_mesh_shading.binding_desc);
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
		model_render_count = model->GetMeshParts().size();
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
