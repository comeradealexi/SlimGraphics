#include "PostProcess.h"
#include "sgUtils.h"
#include <imgui.h>

using namespace sg;

PostProcess::PostProcess(sg::SharedPtr<sg::Device>& _device) : device(_device)
{
	pipeline_binding_desc = {};
	pipeline_binding_desc.uav_binding_count = 1;
	pipeline_binding_desc.srv_binding_count = 2;
	pipeline_binding_desc.cbv_binding_count = 2;

	pipeline_desc = {};

	constant_data = {};
	constant_data.colour_output_enabled = DirectX::XMFLOAT4A(1.0f, 1.0f, 1.0f, 1.0f);
	constant_data.colour_clamping.x = 0.0f;
	constant_data.colour_clamping.y = 1.0f;
}

void PostProcess::Update(HWND hwnd, const se::GameInput& input, float delta_time, float total_time, const Camera& camera, DebugDraw& debug_draw)
{
	if (ImGui::CollapsingHeader("Post Process"))
	{
		ImGui::Checkbox("Enabled", &enabled);
		ImGui::Checkbox("Show Depth Buffer", &show_depth_buffer);
		ImGui::Text("Colour Channel Output");
		ImGui::Checkbox("R", &post_process_output[0]);
		ImGui::SameLine();
		ImGui::Checkbox("G", &post_process_output[1]);
		ImGui::SameLine();
		ImGui::Checkbox("B", &post_process_output[2]);
		ImGui::SameLine();
		ImGui::Checkbox("A", &post_process_output[3]);
		ImGui::SliderInt("Bit Depth (0 = off)", &constant_data.colour_bit_values.x, 0, 8);
		ImGui::Text("Colour Clamping");
		if (ImGui::SliderFloat("Colour Min", &constant_data.colour_clamping.x, 0.0f, 1.0f))
		{
			if (constant_data.colour_clamping.x > constant_data.colour_clamping.y)
				constant_data.colour_clamping.y = constant_data.colour_clamping.x;
		}
		if (ImGui::SliderFloat("Colour Max", &constant_data.colour_clamping.y, 0.0f, 1.0f))
		{
			if (constant_data.colour_clamping.y < constant_data.colour_clamping.x)
				constant_data.colour_clamping.x = constant_data.colour_clamping.y;
		}
		ImGui::Checkbox("Normalize Output", &colour_clamping_normalize);
	}

	constant_data.colour_output_enabled = DirectX::XMFLOAT4A(post_process_output[0] ? 1.0f : 0.0f, post_process_output[1] ? 1.0f : 0.0f, post_process_output[2] ? 1.0f : 0.0f, post_process_output[3] ? 1.0f : 0.0f);
	constant_data.colour_clamping.z = colour_clamping_normalize ? 1.0f : 0.0f;
	constant_data.mode.x = show_depth_buffer ? 1 : 0;

	if (pipeline == nullptr)
	{
		LoadPipeline();
	}
}

bool PostProcess::Render(sg::CommandList& command_list, sg::ConstantBufferView& cbv_camera, sg::UnorderedAccessView out_texture, sg::ShaderResourceView in_colour_texture, sg::ShaderResourceView in_depth_texture, SimpleLinearConstantBuffer& cbuffer, sg::u32 width, sg::u32 height)
{
	if (enabled)
	{
		sg::ConstantBufferView cbv_data = cbuffer.AllocateAndWrite(constant_data);

		Binding b;
		b.uav_binding_count = 1;
		b.cbv_binding_count = 2;
		b.srv_binding_count = 2;

		b.set_uav(out_texture, 0);

		b.set_cbv(cbv_camera, 0);
		b.set_cbv(cbv_data, 1);

		b.set_srv(in_colour_texture, 0);
		b.set_srv(in_depth_texture, 1);


		command_list.set_pipeline(pipeline.get());
		command_list.bind(b, PipelineType::Compute);

		const DispatchModeInfo& mode_info = GetActiveDispatchInfo(dispatch_mode);
		u32 dispatch_x = (width + mode_info.x - 1) / mode_info.x;	// Round up to nearest thread id
		u32 dispatch_y = (height + mode_info.y - 1) / mode_info.y;	// Round up to nearest thread id
		command_list.dispatch(dispatch_x, dispatch_y);
	}

	return enabled;
}

void PostProcess::LoadPipeline()
{
	pipeline_desc.compute_shader = create_compute_shader(*device, GetActiveDispatchInfo(dispatch_mode).shader);
	pipeline = device->create_pipeline(pipeline_desc, pipeline_binding_desc);
}
