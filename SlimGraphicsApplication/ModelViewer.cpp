#include "ModelViewer.h"
#include <sgPlatformInclude.h>
#include <imgui.h>
#include <seEngineBasicFileIO.h>

using namespace sg;

ModelViewer::ModelViewer(SharedPtr<Device>& _device) : render_target_format(DXGI_FORMAT_R8G8B8A8_UNORM), depth_stencil_format(DXGI_FORMAT_D32_FLOAT), device(_device)
{
	pipeline_binding_desc = {};
	pipeline_binding_desc.cbv_binding_count = 2;

	std::vector<uint8_t> vertex_data = se::BasicFileIO::LoadFile("ShaderBinD3D12_Debug\\ModelViewer_VertexShader.PC_DXC");
	std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShaderBinD3D12_Debug\\ModelViewer_PixelShader.PC_DXC");
	shader_vertex = device->create_vertex_shader(vertex_data);
	shader_pixel = device->create_pixel_shader(pixel_data);

	CreatePipeline();

	model_file_list.push_back("../SlimGraphicsAssets/DebugModels/teapot.obj");


	model_init_data.file_path = model_file_list[0];
}

void ModelViewer::Update(float delta_time, float total_time)
{
	bool recreate_pipeline = false;

	ImGui::Begin("Model Viewer", nullptr, 0);

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
		if (model_file_list_current != list_changed) recreate_model = true;
		
		ImGui::SliderFloat("Scale", &model_scale, 0.0f, 10.0f);
		ImGui::SliderFloat("Render Percent", &render_percentage, 0.0f, 1.0f);
		ImGui::Checkbox("Shade Primitive Order", &render_primitive_order);		
	}

	// Mesh opt etc

	// Pipeline state
	if (ImGui::CollapsingHeader("Pipeline"))
	{
		recreate_pipeline |= ImGui::Checkbox("Wireframe", &render_wireframe);
		recreate_pipeline |= ImGui::Combo("Cull Mode", &cull_mode, "Back\0Front\0None\0", 3);
	}
	
	ImGui::End();

	// Model constant buffer
	model_data.model_matrix = DirectX::XMMatrixScaling(model_scale, model_scale, model_scale);
	model_data.time_frame_delta = delta_time;
	model_data.time_total = total_time;
	model_data.shade_primitive_order = render_primitive_order;

	if (recreate_pipeline)
	{ 
		CreatePipeline();
	}

}

void ModelViewer::Render(CommandList& command_list, ConstantBufferView& cbv_camera, Ptr<UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer)
{
	if (recreate_model)
	{
		CreateModel(upload_heap);
		recreate_model = false;
	}

	if (model)
	{
		command_list.set_pipeline(pipeline.get());

		Binding b;
		b.cbv_binding_count = 2;
		b.set_cbv(cbv_camera, 0);


		command_list.bind_vertex_buffer(model->GetVertexBufferView());
		command_list.bind_index_buffer(model->GetIndexBufferView());
		for (Model::MeshPart mesh_part : model->GetMeshParts())
		{
			model_data.primitive_count = mesh_part.draw_count / 3;
			sg::ConstantBufferView cbv_model = cbuffer.AllocateAndWrite(model_data);
			b.set_cbv(cbv_model, 1);
			command_list.bind(b, PipelineType::Geometry);

			command_list.draw_indexed_instanced(static_cast<sg::u32>(mesh_part.draw_count * render_percentage), 1, mesh_part.ib_offset, mesh_part.vb_offset, 0);
		}
	}
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
	const Rasterizer::CullMode cull_values[] = { Rasterizer::CullMode::Back, Rasterizer::CullMode::Front, Rasterizer::CullMode::None };
	pipeline_desc.rasterizer_desc.cull_mode = cull_values[cull_mode];
	pipeline = device->create_pipeline(pipeline_desc, pipeline_binding_desc);
	seAssert(pipeline != nullptr, "Failed to create model view pipeline");
}

void ModelViewer::CreateModel(Ptr<UploadHeap>& upload_heap)
{
	model = Ptr<Model>(new Model(device.get(), upload_heap.get(), model_init_data));
}