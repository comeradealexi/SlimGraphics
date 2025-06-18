#include "BasicGrid.h"
#include <sgPlatformInclude.h>
#include <imgui.h>
#include <seEngineBasicFileIO.h>
#include "Camera.h"

using namespace sg;

BasicGrid::BasicGrid(sg::SharedPtr<sg::Device>& _device)
{
	std::vector<uint8_t> vertex_data = se::BasicFileIO::LoadFile("ShaderBinD3D12_Debug\\BasicGrid_VertexShader.PC_DXC");
	std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShaderBinD3D12_Debug\\BasicGrid_PixelShader.PC_DXC");
	vs = _device->create_vertex_shader(vertex_data);
	ps = _device->create_pixel_shader(pixel_data);

	sg::BindingDesc pipeline_binding_desc;
	pipeline_binding_desc.cbv_binding_count = 1;

	sg::PipelineDesc::Graphics pipeline_desc;
	pipeline_desc.input_layout = {};
	pipeline_desc.vertex_shader = vs.get();
	pipeline_desc.pixel_shader = ps.get();
	pipeline_desc.render_target_count = 1;
	pipeline_desc.render_target_format_list[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pipeline_desc.depth_stencil_format = DXGI_FORMAT_D32_FLOAT;
	pipeline_desc.depth_stencil_desc.depth_enable = true;
	pipeline_desc.depth_stencil_desc.depth_write = false;
	pipeline_desc.rasterizer_desc.fill_mode = Rasterizer::FillMode::Solid;
	pipeline_desc.rasterizer_desc.cull_mode = Rasterizer::CullMode::Back;
	pipeline_desc.blend_desc.render_targets[0].blend_enable = true;
	pipeline_desc.blend_desc.render_targets[0].src_blend = Blend::Type::Src_alpha;
	pipeline_desc.blend_desc.render_targets[0].dest_blend = Blend::Type::Inv_src_alpha;
	pipeline = _device->create_pipeline(pipeline_desc, pipeline_binding_desc);
	seAssert(pipeline != nullptr, "Failed to create model view pipeline");
}

void BasicGrid::Update(float delta_time, float total_time, const Camera& camera)
{

}

void BasicGrid::Render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer)
{
	command_list.set_pipeline(pipeline.get());

	Binding b;
	b.cbv_binding_count = 1;
	b.set_cbv(cbv_camera, 0);
	command_list.bind(b, PipelineType::Geometry);
	command_list.draw_instanced(6, 1, 0, 0);
}
