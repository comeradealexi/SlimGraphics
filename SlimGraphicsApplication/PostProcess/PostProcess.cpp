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
	constant_data.frac_output.y = 1.0f;
}

void PostProcess::Update(HWND hwnd, const se::GameInput& input, float delta_time, float total_time, const Camera& camera, DebugDraw& debug_draw)
{
	if (ImGui::CollapsingHeader("Post Process"))
	{
		ImGui::Checkbox("Enabled", &enabled);
		ImGui::RadioButton("Colour Target", (int*)&post_process_technique, (int)PostProcessTechnique::ShowColourTarget);
		ImGui::RadioButton("Depth Target", (int*)&post_process_technique, (int)PostProcessTechnique::ShowDepthTarget);
		ImGui::RadioButton("SV_GroupID", (int*)&post_process_technique, (int)PostProcessTechnique::ShowSVGroupID);
		ImGui::RadioButton("SV_GroupThreadID", (int*)&post_process_technique, (int)PostProcessTechnique::ShowSVGroupThreadID);
		ImGui::RadioButton("SV_DispatchThreadID", (int*)&post_process_technique, (int)PostProcessTechnique::ShowSVDispatchThreadID);

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
		ImGui::Text("Frac Clamping");
		ImGui::Checkbox("Frac Enabled", &frac_enabled);
		ImGui::SliderFloat("Frac Multipler", &constant_data.frac_output.y, 1.0f, 40.0f);
		
		bool modified = false;
		ImGui::Text("Dispatch Counts:");
		modified |= ImGui::RadioButton("4x4", (int*)&dispatch_mode, (int)DispatchMode::DM_4x4); ImGui::SameLine();
		modified |= ImGui::RadioButton("8x4", (int*)&dispatch_mode, (int)DispatchMode::DM_8x4); ImGui::SameLine();
		modified |= ImGui::RadioButton("8x8", (int*)&dispatch_mode, (int)DispatchMode::DM_8x8); ImGui::SameLine();
		modified |= ImGui::RadioButton("16x8", (int*)&dispatch_mode, (int)DispatchMode::DM_16x8); ImGui::SameLine();
		modified |= ImGui::RadioButton("16x16", (int*)&dispatch_mode, (int)DispatchMode::DM_16x16);
		
		ImGui::Text("Optimisations:");
		ImGui::Checkbox("Grid Tile Optimisation", &grid_optimisation);
		ImGui::Checkbox("Write To Original Dispatch Thread ID", &write_to_original_dispatch_thread_id);

		if (modified)
		{
			pipeline = nullptr;
		}
	}

	constant_data.colour_output_enabled = DirectX::XMFLOAT4A(post_process_output[0] ? 1.0f : 0.0f, post_process_output[1] ? 1.0f : 0.0f, post_process_output[2] ? 1.0f : 0.0f, post_process_output[3] ? 1.0f : 0.0f);
	constant_data.colour_clamping.z = colour_clamping_normalize ? 1.0f : 0.0f;
	constant_data.mode.w = (int)post_process_technique;
	constant_data.frac_output.x = frac_enabled ? 1.0f : 0.0f;
	constant_data.optimisations.x = grid_optimisation ? 1 : 0;
	constant_data.optimisations.y = write_to_original_dispatch_thread_id ? 1 : 0;

	if (pipeline == nullptr)
	{
		LoadPipeline(); 
	}
}

bool PostProcess::Render(sg::CommandList& command_list, sg::ConstantBufferView& cbv_camera, sg::UnorderedAccessView out_texture, sg::ShaderResourceView in_colour_texture, sg::ShaderResourceView in_depth_texture, SimpleLinearConstantBuffer& cbuffer, sg::u32 width, sg::u32 height)
{
	if (enabled)
	{
		const DispatchModeInfo& mode_info = GetActiveDispatchInfo(dispatch_mode);
		u32 dispatch_x = (width + mode_info.x - 1) / mode_info.x;	// Round up to nearest thread id
		u32 dispatch_y = (height + mode_info.y - 1) / mode_info.y;	// Round up to nearest thread id
		u32 dispatch_z = 1;

		constant_data.mode.x = (int32_t)dispatch_x;
		constant_data.mode.y = (int32_t)dispatch_y;
		constant_data.mode.z = (int32_t)dispatch_z;

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


		command_list.dispatch(dispatch_x, dispatch_y, dispatch_z);
	}

	return enabled;
}

void PostProcess::LoadPipeline()
{
	pipeline_desc.compute_shader = create_compute_shader(*device, GetActiveDispatchInfo(dispatch_mode).shader);
	pipeline = device->create_pipeline(pipeline_desc, pipeline_binding_desc);
}
