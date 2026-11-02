#include "BasicGrid.h"
#include <sgPlatformInclude.h>
#include <imgui.h>
#include <seEngineBasicFileIO.h>
#include "Camera.h"
#include <Utils.h>
#include <sgUtils.h>

using namespace sg;

BasicGrid::BasicGrid(sg::SharedPtr<sg::Device>& _device)
{
	sg::SharedPtr<sg::VertexShader> vs = create_vertex_shader(*_device, get_shader_path("BasicGrid_VertexShader"));
	sg::SharedPtr<sg::PixelShader> ps = create_pixel_shader(*_device, get_shader_path("BasicGrid_PixelShader"));

	sg::BindingDesc pipeline_binding_desc;
	pipeline_binding_desc.cbv_binding_count = 1;

	sg::PipelineDesc::Graphics pipeline_desc;
	pipeline_desc.input_layout = {};
	pipeline_desc.vertex_shader = vs;
	pipeline_desc.pixel_shader = ps;
	pipeline_desc.render_target_count = 1;
	pipeline_desc.render_target_format_list[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pipeline_desc.depth_stencil_format = DXGI_FORMAT_D32_FLOAT;
	pipeline_desc.depth_stencil_desc.depth_enable = true;
	pipeline_desc.depth_stencil_desc.depth_write = false;
	pipeline_desc.rasterizer_desc.fill_mode = Rasterizer::FillMode::Solid;
	pipeline_desc.rasterizer_desc.cull_mode = Rasterizer::CullMode::None;
	pipeline_desc.blend_desc.render_targets[0].blend_enable = true;
	pipeline_desc.blend_desc.render_targets[0].src_blend = Blend::Type::Src_alpha;
	pipeline_desc.blend_desc.render_targets[0].dest_blend = Blend::Type::Inv_src_alpha;
	pipeline = _device->create_pipeline(pipeline_desc, pipeline_binding_desc);
	seAssert(pipeline != nullptr, "Failed to create model view pipeline");
}

void BasicGrid::Update(float delta_time, float total_time, const Camera& camera)
{
	bool show = ImGui::CollapsingHeader("Grid");
	if (show)
	{
		ImGui::Checkbox("Enabled", &active);
	}
}

void BasicGrid::Render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer)
{
	if (active)
	{
		command_list.set_pipeline(pipeline.get());

		Binding b;
		b.cbv_binding_count = 1;
		b.set_cbv(cbv_camera, 0);
		command_list.bind(b, PipelineType::Geometry);
		command_list.draw_instanced(6, 1, 0, 0);
	}
}
