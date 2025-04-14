#include "ModelViewer.h"
#include <sgPlatformInclude.h>
#include <imgui.h>
#include <seEngineBasicFileIO.h>

using namespace sg;

ModelViewer::ModelViewer(SharedPtr<Device>& _device) : render_target_format(DXGI_FORMAT_R8G8B8A8_UNORM), depth_stencil_format(DXGI_FORMAT_D32_FLOAT), device(_device)
{
	pipeline_binding_desc = {};
	pipeline_binding_desc.cbv_binding_count = 2;

	std::vector<uint8_t> vertex_data = se::BasicFileIO::LoadFile("TODO");
	std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("TODO");
	shader_vertex = device->create_vertex_shader(vertex_data);
	shader_pixel = device->create_pixel_shader(pixel_data);
}

void ModelViewer::Update(float delta_time, float total_time)
{
	bool bRecreatePipeline = false;
	bool bRecreateModel = false;

	ImGui::Begin("Model Viewer", nullptr, 0);

	// Model scale and shading etc
	if (ImGui::CollapsingHeader("Model"))
	{
		ImGui::SliderFloat("Scale", &model_scale, 0.0f, 10000.0f);
	}

	// Mesh opt etc

	// Pipeline state
	if (ImGui::CollapsingHeader("Pipeline"))
	{
		ImGui::Checkbox("Wireframe", &render_wireframe);
	}
	
	ImGui::End();

	// Model constant buffer
	model_data.model_matrix = DirectX::XMMatrixScaling(model_scale, model_scale, model_scale);

	if (bRecreatePipeline)
	{ 
		CreatePipeline();
	}
	if (bRecreateModel)
	{
		CreateModel();
	}
}

void ModelViewer::Render(CommandList& command_list, ConstantBufferView& cbv_camera, Ptr<UploadHeap>& upload_heap)
{

}

void ModelViewer::CreatePipeline()
{
	pipeline_desc.input_layout = Model::Vertex::make_input_layout();
	pipeline_desc.vertex_shader = shader_vertex.get();
	pipeline_desc.pixel_shader = shader_pixel.get();
	pipeline_desc.render_target_count = 1;
	pipeline_desc.render_target_format_list[0] = render_target_format;
	pipeline_desc.depth_stencil_format = DXGI_FORMAT_D32_FLOAT;
	pipeline_desc.depth_stencil_desc.depth_enable = true;
	pipeline_desc.depth_stencil_desc.depth_write = true;
	pipeline_desc.rasterizer_desc.fill_mode = render_wireframe ? Rasterizer::FillMode::Wireframe : Rasterizer::FillMode::Solid;
	pipeline = device->create_pipeline(pipeline_desc, pipeline_binding_desc);
}

void ModelViewer::CreateModel(Ptr<UploadHeap>& upload_heap)
{
	Model::InitData init_data;
	model = Ptr<Model>(new Model(device.get(), upload_heap.get(), init_data));
}