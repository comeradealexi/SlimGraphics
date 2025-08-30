
#include "MagnifyingGlass.h"
#include "sgUtils.h"
#include <imgui.h>

using namespace sg;

MagnifyingGlass::MagnifyingGlass(sg::SharedPtr<sg::Device>& _device) : device(_device)
{
	ResourceCreateDesc rcd(target_size, target_size, DXGI_FORMAT_R8G8B8A8_UNORM, ResourceUsageFlags::RenderTarget);
	render_texture = create_texture(*device, rcd);
	render_target_view = device->create_render_target_view(render_texture);

	constant_data.scale.x = 0.5f;

	BindingDesc bd;
	bd.cbv_binding_count = 2;
	bd.srv_binding_count = 1;

	PipelineDesc::Graphics pd;
	pd.vertex_shader = create_vertex_shader(*device, "ShaderBin_Debug\\MagnifyingGlass_VS.PC_DXC");
	pd.pixel_shader = create_pixel_shader(*device, "ShaderBin_Debug\\MagnifyingGlass_PS.PC_DXC");
	pd.render_target_format_list[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pd.render_target_count = 1;
	pd.depth_stencil_desc.depth_enable = false;
	pipeline = device->create_pipeline(pd, bd);
}

void MagnifyingGlass::Update(HWND hwnd, const se::GameInput& input, float delta_time, float total_time, const Camera& camera, DebugDraw& debug_draw)
{
	active = ImGui::CollapsingHeader("Magnifying Glass");
	if (active)
	{
		ImGui::SliderFloat("Zoom", &constant_data.scale.x, 0.0f, 1.0f);
		ImGui::Image((ImTextureID)device->imgui_texture_viewer_handle().ptr, ImVec2(device->imgui_texture_viewer_data().texture->resource_create_desc.width, device->imgui_texture_viewer_data().texture->resource_create_desc.height));

		constant_data.coordinates.z = target_size;
		constant_data.coordinates.w = target_size;

		POINT c;
		if (GetCursorPos(&c))
		{
			if (ScreenToClient(hwnd, &c))
			{
				constant_data.coordinates.x = c.x;
				constant_data.coordinates.y = c.y;
			}
		}
	}
}

void MagnifyingGlass::Render(sg::CommandList& command_list, sg::ConstantBufferView& cbv_camera, ShaderResourceView read_texture, SimpleLinearConstantBuffer& cbuffer)
{
	if (active)
	{
		sg::ConstantBufferView cbv_data = cbuffer.AllocateAndWrite(constant_data);

		Viewport vp;
		vp.width = (u32)target_size;
		vp.height = (u32)target_size;
		ScissorRect sc;
		sc.right = (u32)target_size;
		sc.bottom = (u32)target_size;

		Binding b;
		b.cbv_binding_count = 2;
		b.srv_binding_count = 1;
		b.set_cbv(cbv_camera, 0);
		b.set_cbv(cbv_data, 1);
		b.set_srv(read_texture, 0);
		command_list.start_geometry_pass(1, &render_target_view, vp, sc);
		{
			command_list.set_pipeline(pipeline.get());
			command_list.bind(b, PipelineType::Geometry);
			command_list.draw_instanced(3, 1, 0, 0);
		}
		command_list.end_geometry_pass();
	}
}

