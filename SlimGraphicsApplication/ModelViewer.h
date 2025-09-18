#pragma once
#include <sgPlatformInclude.h>
#include "UploadHeap.h"
#include "Model.h"
#include "ShaderSharedStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "DebugDraw.h"

class ModelViewer
{
public:
	ModelViewer(sg::SharedPtr<sg::Device>& _device);
	
	void Update(float delta_time, float total_time, const Camera& camera, DebugDraw& debug_draw);
	void Render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw);
	void CreatePipeline();
	void CreateModel(sg::Ptr<UploadHeap>& upload_heap);
	bool MeshPartVisible(const Camera& camera, DirectX::XMFLOAT3 position, Model::MeshPart& mesh_part);

private:
	const DXGI_FORMAT render_target_format;
	const DXGI_FORMAT depth_stencil_format;

	int model_file_list_current = 0;
	std::vector<std::string> model_file_list;
	enum class RenderGeo : int
	{
		Model,
		FullscreenTriangle,
		FullscreenQuad
	};
	RenderGeo render_geo = RenderGeo::Model;

	sg::SharedPtr<sg::Device> device;
	sg::SharedPtr<sg::Pipeline> pipeline;
	sg::SharedPtr<sg::Pipeline> pipeline_eds;
	sg::SharedPtr<sg::Pipeline> pipeline_fullscreen_triangle;
	sg::SharedPtr<sg::Pipeline> pipeline_fullscreen_quad;

	sg::BindingDesc pipeline_binding_desc;
	sg::PipelineDesc::Graphics pipeline_desc;
	sg::SharedPtr<sg::VertexShader> shader_vertex;
	sg::SharedPtr<sg::VertexShader> shader_vertex_triangle;
	sg::SharedPtr<sg::VertexShader> shader_vertex_quad;
	sg::SharedPtr<sg::PixelShader> shader_pixel;
	sg::SharedPtr<sg::PixelShader> shader_pixel_eds;
	sg::Ptr<Model> model;
	Model::InitData model_init_data;
	bool* render_model_bool_array = nullptr;

	// constant data
	ShaderStructs::ModelData model_data = {};
	float model_scale = 1.0f;

	// ImGui toggles
	enum class RenderMode : int
	{
		Default,
		PrimitiveOrder,
		VertexOrder,
		PixelOrder,
		MeshletOrder,
		MeshletCullAngle,
		WaveIntrinsics,
		AmplificationOrder,
	} render_mode = RenderMode::Default;
	int cull_mode = 0;
	bool recreate_model = true;
	bool render_wireframe = false;
	float render_percentage = 1.0f;
	bool render_as_mesh_shader = true;
	bool depth_enable = true;
	bool depth_write = true;
	bool scale_model_to_1 = false;
	float scale_mode_to_1_previous = model_scale;
	
	// Early Depth Stencil https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-attributes-earlydepthstencil
	// Without EDS, the UAV we write to per-pixel causes every single rasterised pixel to invoke the pixel shader
	// so rendering front to back has zero impact on the number of pixel shaders invoked
	// by forcing EDS we make it invoke the pixel shader so it is more like traditional rendering.
	bool use_eds = true; 
	
	bool mesh_shader_cone_culling = true;
	bool mesh_shader_sphere_frustum_culling = true;
	bool amplification_mesh_shader = true;


	// Uav
	sg::SharedPtr<sg::Memory> uav_memory;
	sg::SharedPtr<sg::Buffer> uav_buffer;
	sg::UnorderedAccessView uav;
	sg::ShaderResourceView srv;

	// Uav Readback
	sg::SharedPtr<sg::Memory> readback_uav_memory;
	sg::SharedPtr<sg::Buffer> readback_uav_buffer;


	bool pixel_shade_order_ranged_colour = false;
	bool pixel_shade_order_automatic = false;
	bool pixel_shade_order_coloured = true;
	float pixel_shade_order_automated_speed = 1.0f;
	float pixel_shade_order_pixel_to_shade = 1.0f;
	float pixel_shade_order_range = 0.0f;

	// rotate
	bool rotate_model = false;
	float rotate_value = 0.0f;

	struct MeshShaderRendering
	{
		sg::SharedPtr<sg::Pipeline> pipeline;
		sg::SharedPtr<sg::Pipeline> pipeline_eds;
		sg::PipelineDesc::Mesh pipeline_desc;
		sg::SharedPtr<sg::AmplificationShader> shader_amplification;
		sg::SharedPtr<sg::MeshShader> shader_mesh;
		sg::SharedPtr<sg::PixelShader> shader_pixel;
		sg::SharedPtr<sg::PixelShader> shader_pixel_eds;
		sg::BindingDesc binding_desc;
	} mesh_shading, amplification_mesh_shading;
	
	struct CPUCulling
	{
		bool accurate_cull_check = true;
		bool cull_all = false;
		bool cull_none = false;

		int stat_passed = 0;
		int stat_failed = 0;
	} cpu_culling;

	struct CPUSorting
	{
		bool sort = false;
		bool front_to_back = true;

	} cpu_sorting;

	struct DebugMeshDrawing
	{
		bool render_model_parts_aabb = false;
		bool render_model_parts_sphere = false;
		bool render_meshlet_parts = false;
	} debug_drawing;
	
	enum class WaveIntrinsicRenderMode : int
	{
		LaneIndices,
		LaneOrder,
		WaveUsage,
		HelperLaneViewer,
		WaveCount,
		AllWavesSameValue,
	} wave_intrinsic_render_mode = WaveIntrinsicRenderMode::LaneIndices;
};

	