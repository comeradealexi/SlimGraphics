#pragma once
#include <sgPlatformInclude.h>
#include "UploadHeap.h"
#include "Model.h"
#include "ShaderSharedStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"

class ModelViewer
{
public:
	ModelViewer(sg::SharedPtr<sg::Device>& _device);
	
	void Update(float delta_time, float total_time, const Camera& camera);
	void Render(sg::CommandList& command_list, sg::ConstantBufferView& cbv_camera, sg::Ptr<UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer);
	void CreatePipeline();
	void CreateModel(sg::Ptr<UploadHeap>& upload_heap);

private:
	const DXGI_FORMAT render_target_format;
	const DXGI_FORMAT depth_stencil_format;

	int model_file_list_current = 0;
	std::vector<std::string> model_file_list;
	bool render_fullscreen_triangle = false;
	bool render_fullscreen_quad = false;

	sg::SharedPtr<sg::Device> device;
	sg::Ptr<sg::Pipeline> pipeline;
	sg::Ptr<sg::Pipeline> pipeline_fullscreen_triangle;
	sg::Ptr<sg::Pipeline> pipeline_fullscreen_quad;

	sg::BindingDesc pipeline_binding_desc;
	sg::PipelineDesc::Graphics pipeline_desc;
	sg::Ptr<sg::VertexShader> shader_vertex;
	sg::Ptr<sg::VertexShader> shader_vertex_triangle;
	sg::Ptr<sg::VertexShader> shader_vertex_quad;
	sg::Ptr<sg::PixelShader> shader_pixel;
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
	} render_mode;
	int cull_mode = 0;
	bool recreate_model = true;
	bool render_wireframe = false;
	float render_percentage = 1.0f;


	// Uav
	sg::SharedPtr<sg::Memory> uav_memory;
	sg::SharedPtr<sg::Buffer> uav_buffer;
	sg::UnorderedAccessView uav;
	sg::ShaderResourceView srv;
	bool pixel_shade_order_ranged_colour = false;
	bool pixel_shade_order_automatic = false;
	bool pixel_shade_order_coloured = false;
	float pixel_shade_order_automated_speed = 1.0f;
	float pixel_shade_order_pixel_to_shade = 1.0f;
	float pixel_shade_order_range = 0.0f;

	// rotate
	bool rotate_model = false;
	float rotate_value = 0.0f;
};

