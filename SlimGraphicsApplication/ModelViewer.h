#pragma once
#include <sgPlatformInclude.h>
#include "UploadHeap.h"
#include "Model.h"
#include "ShaderSharedStructures.h"
#include "LinearConstantBuffer.h"

class ModelViewer
{
public:
	ModelViewer(sg::SharedPtr<sg::Device>& _device);
	
	void Update(float delta_time, float total_time);
	void Render(sg::CommandList& command_list, sg::ConstantBufferView& cbv_camera, sg::Ptr<UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer);
	void CreatePipeline();
	void CreateModel(sg::Ptr<UploadHeap>& upload_heap);

private:
	const DXGI_FORMAT render_target_format;
	const DXGI_FORMAT depth_stencil_format;

	int model_file_list_current = 0;
	std::vector<std::string> model_file_list;

	sg::SharedPtr<sg::Device> device;
	sg::Ptr<sg::Pipeline> pipeline;
	sg::BindingDesc pipeline_binding_desc;
	sg::PipelineDesc::Graphics pipeline_desc;
	sg::Ptr<sg::VertexShader> shader_vertex;
	sg::Ptr<sg::PixelShader> shader_pixel;
	sg::Ptr<Model> model;
	Model::InitData model_init_data;

	// constant data
	ShaderStructs::ModelData model_data;
	float model_scale = 1.0f;

	// ImGui toggles
	enum class RenderMode : int
	{
		Default,
		PrimitiveOrder,
		VertexOrder
	} render_mode;
	int cull_mode = 0;
	bool recreate_model = true;
	bool render_wireframe = false;
	float render_percentage = 1.0f;
};

