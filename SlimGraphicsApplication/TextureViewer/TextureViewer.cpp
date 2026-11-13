#include "TextureViewer.h"
#include <imgui.h>
#include "sgUtils.h"
#include "Utils.h"

using namespace sg;

TextureViewer::TextureViewer(sg::SharedPtr<sg::Device>& _device) : device(_device)
{
	imgui_texture_viewer = device->allocate_imgui_texture_viewer_data();

	BindingDesc bd;
	bd.srv_binding_count = 1;

	PipelineDesc::Graphics pd;
	pd.vertex_shader = create_vertex_shader(*device, get_shader_path("TextureViewer_VS"));
	pd.pixel_shader = create_pixel_shader(*device, get_shader_path("TextureViewer_PS"));
	pd.render_target_format_list[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pd.render_target_count = 1;
	pd.depth_stencil_desc.depth_enable = false;
	pipeline = device->create_pipeline(pd, bd);

}

void TextureViewer::set_texture(sg::SharedPtr<sg::Texture> texture)
{
	view_texture = texture;
	view_texture_srv = device->create_shader_resource_view(texture);
}

void TextureViewer::imgui_update()
{
	if (ImGui::Begin("Texture Viewer"))
	{
		ImGui::Checkbox("Resize Target to Texture", &resize_target_to_texture);
		ImGui::BeginDisabled(resize_target_to_texture);
		ImGui::InputInt("Target Width", (int*)&target_width);
		ImGui::InputInt("Target Height", (int*)&target_height);
		ImGui::EndDisabled();

		if (view_texture)
		{
			if (resize_target_to_texture)
			{
				target_width = view_texture->resource_create_desc.width;
				target_height = view_texture->resource_create_desc.height;
			}

			if (!render_texture || (render_texture->resource_create_desc.width != target_width || render_texture->resource_create_desc.height != target_height))
			{
				ResourceCreateDesc rcd(target_width, target_height, DXGI_FORMAT_R8G8B8A8_UNORM, ResourceUsageFlags::RenderTarget);
				render_texture = create_texture(*device, rcd);
				render_target_view = device->create_render_target_view(render_texture);
				device->set_imgui_texture_viewer_data(imgui_texture_viewer, render_texture);
			}

			ImGui::Image((ImTextureID)imgui_texture_viewer.gpu_handle.ptr, ImVec2(imgui_texture_viewer.texture->resource_create_desc.width, imgui_texture_viewer.texture->resource_create_desc.height));
		}

		ImGui::End();
	}
}

void TextureViewer::render(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer)
{
	if (view_texture && render_texture)
	{
		Viewport vp;
		vp.width = (u32)target_width;
		vp.height = (u32)target_height;
		ScissorRect sc;
		sc.right = (u32)target_width;
		sc.bottom = (u32)target_height;

		Binding b;
		b.srv_binding_count = 1;
		b.set_srv(view_texture_srv, 0);
		command_list.start_geometry_pass(1, &render_target_view, vp, sc);
		{
			float4 colour = { 0.0f,0.0f,0.0f,0.0f };
			command_list.clear_render_target_view(render_target_view, colour);
			command_list.set_pipeline(pipeline.get());
			command_list.bind(b, PipelineType::Geometry);
			command_list.draw_instanced(3, 1, 0, 0);
		}
		command_list.end_geometry_pass();
	}
}
