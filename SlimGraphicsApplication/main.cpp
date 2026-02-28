#include <sgPlatformInclude.h>
#include <seEngineBasicFileIO.h>
#include <Win32/seWindow.h>
#include <Win32/seGameInput.h>
#include <imgui.h>
#include <implot.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <DirectXMath.h>
#include "AverageTimer.h"
#include "BasicGrid.h"
#include <sgUtils.h>

//Application headers
#include "Model.h"
#include "sgUploadHeap.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "ModelViewer.h"
#include "DebugDraw.h"
#include "Scene/Terrain.h"
#include "MagnifyingGlass/MagnifyingGlass.h"
#include "PostProcess/PostProcess.h"
#include "BitonicSort.h"

/*
TODO:
Compute
Vertex Buffers
Index Buffers
Texture Bindings
UAV
*/

void OverrideImguiStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	// Corners
	style.WindowRounding = 8.0f;
	style.ChildRounding = 8.0f;
	style.FrameRounding = 6.0f;
	style.PopupRounding = 6.0f;
	style.ScrollbarRounding = 6.0f;
	style.GrabRounding = 6.0f;
	style.TabRounding = 6.0f;

	// Colors
	colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	colors[ImGuiCol_Border] = ImVec4(0.43f, 0.50f, 0.56f, 0.50f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.50f, 0.56f, 0.50f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
	colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.56f, 1.00f, 0.80f);
	colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.22f, 0.36f, 1.00f);
	//colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
	//colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 0.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

using namespace sg;

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
#if false
	while (!IsDebuggerPresent())
	{
		Sleep(0);
	}
#endif
	const DXGI_FORMAT back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;
	const u32 frame_count = 3;
	u64 w = 1920;
	u64 h = 1080;
	u64 x_off = 0;
	u64 y_off = 0;
	se::Ptr<se::Window> wnd(new se::Window(hInstance, nCmdShow, w,h));
	{
		RECT cr;
		GetClientRect(wnd->g_hWnd, &cr);
		w = cr.right - cr.left;
		h = cr.bottom - cr.top;

		POINT xy;
		xy.x = cr.left;
		xy.y = cr.top;
		ClientToScreen(wnd->g_hWnd, &xy);
		x_off = xy.x;
		y_off = xy.y;
	}
	se::Ptr<se::GameInput> input(new se::GameInput());

	SharedPtr<Device> device(new Device());
	SharedPtr<CommandQueue> queue = device->create_command_queue();
	SharedPtr<CommandList> command_buffer = device->create_command_buffer();
	auto fence = device->create_queue_fence();

	{
		sg::SamplerDesc default_sampler;
		device->AddSamplers("Diffuse", default_sampler);
		device->AddSamplers("Specular", default_sampler);
		device->AddSamplers("Normals", default_sampler);
	}

	SharedPtr<Memory> mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024);
	mem = nullptr;

	//ImGui
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImPlot::CreateContext();
		ImGui::StyleColorsDark();
#ifdef SE_IMGUI_DOCKING
		ImGui::GetIO().ConfigFlags |= (ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable);
#endif
		ImGui_ImplWin32_Init(wnd->g_hWnd);
		ShowWindow(wnd->g_hWnd, SW_SHOWNORMAL);
		device->imgui_init(frame_count, back_buffer_format, DXGI_FORMAT_UNKNOWN, queue.get());
		OverrideImguiStyle();
	}
	
	{//Res size test
		ResourceCreateDesc rd;
		rd.width = 128;
		rd.height = 128;
		rd.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rd.usage_flags = ResourceUsageFlags::UnorderedAccess;
		SizeAndAlignment sal = device->calculate_resource_size_alignment(rd);
	}

	//Load Shaders
	SharedPtr<VertexShader> vs = sg::create_vertex_shader(*device, "ShaderBin_Debug\\Basic_VertexShader.PC_DXC");
	SharedPtr<PixelShader> ps = sg::create_pixel_shader(*device, "ShaderBin_Debug\\Basic_PixelShader.PC_DXC");
	SharedPtr<PixelShader> ps_cb = sg::create_pixel_shader(*device, "ShaderBin_Debug\\Basic_ConstantBuffer_PixelShader.PC_DXC");
	SharedPtr<PixelShader> ps_b = sg::create_pixel_shader(*device, "ShaderBin_Debug\\Basic_Buffer_PixelShader.PC_DXC");
	SharedPtr<ComputeShader> cs = sg::create_compute_shader(*device, "ShaderBin_Debug\\Basic_ComputeShader.PC_DXC");

	SharedPtr<RenderTargetView> backbuffer_rtvs[frame_count];
	u32 current_frame_idx = device->create_swap_chain(wnd->g_hWnd, queue.get(), frame_count, back_buffer_format, w, h, backbuffer_rtvs);

	// Depth buffer
	SharedPtr<DepthStencilView> dsv;
	SharedPtr<Texture> dsv_tex;
	ShaderResourceView depth_srv;
	{
		ResourceCreateDesc rd(w, h, DXGI_FORMAT_D32_FLOAT, ResourceUsageFlags::DepthStencil);
		dsv_tex = sg::create_texture(*device, rd);
		dsv = device->create_depth_stencil_view(dsv_tex);
		depth_srv = device->create_shader_resource_view(dsv_tex);
	}

	// Output render target
	SharedPtr<Texture> final_render_target;
	SharedPtr<RenderTargetView> final_rtv;
	ShaderResourceView final_srv;

	SharedPtr<Texture> intermediate_render_target;
	ShaderResourceView intermediate_srv;
	UnorderedAccessView intermediate_uav;
	SharedPtr<RenderTargetView> intermediate_rtv;
	{
		ResourceCreateDesc rcd = {};
		rcd.width = w;
		rcd.height = h;
		rcd.format = back_buffer_format;
		rcd.usage_flags = ResourceUsageFlags::RenderTarget | ResourceUsageFlags::UnorderedAccess;
		final_render_target = create_texture(*device, rcd);
		final_rtv = device->create_render_target_view(final_render_target);
		final_srv = device->create_shader_resource_view(final_render_target);

		intermediate_render_target = create_texture(*device, rcd);
		intermediate_uav = device->create_unordered_access_view(intermediate_render_target);
		intermediate_srv = device->create_shader_resource_view(intermediate_render_target);
		intermediate_rtv = device->create_render_target_view(intermediate_render_target);

	}

	//Pipeline
	SharedPtr<Pipeline> pipeline;
	SharedPtr<Pipeline> pipeline_cb;
	SharedPtr<Pipeline> pipeline_b;
	SharedPtr<Pipeline> pipeline_compute;
	{
		BindingDesc bd;
		//bd.cbv_binding_count = 1;

		PipelineDesc::Graphics desc;
		desc.pixel_shader = ps;
		desc.vertex_shader = vs;
		desc.render_target_count = 1;
		desc.render_target_format_list[0] = back_buffer_format;
		desc.depth_stencil_desc.depth_enable = false;
		desc.depth_stencil_desc.depth_write = false;
		pipeline = device->create_pipeline(desc, bd);

		bd.cbv_binding_count = 1;
		desc.pixel_shader = ps_cb;
		pipeline_cb = device->create_pipeline(desc, bd);

		bd.cbv_binding_count = 0;
		bd.srv_binding_count = 1;
		desc.pixel_shader = ps_b;
		pipeline_b = device->create_pipeline(desc, bd);
	}
	{
		BindingDesc bd;
		bd.uav_binding_count = 1;
		PipelineDesc::Compute desc;
		desc.compute_shader = cs;
		pipeline_compute = device->create_pipeline(desc, bd);
	}

	//CB
	mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024);
	SharedPtr<Buffer> cbfr = device->create_buffer(mem, 64ull * 1024, BufferType::Constant, false);
	ConstantBufferView cbv = device->create_constant_buffer_view(cbfr.get(), 0, 256);

	SharedPtr<Memory> upload_heap = device->allocate_memory(MemoryType::Upload, MemorySubType::None, 64ull * 1024);
	SharedPtr<Buffer> upload_buffer = device->create_buffer(upload_heap, 64ull * 1024, BufferType::Upload, false);
	{
		float data[4] = { 1.0f,0.0f,0.0f,1.0f };
		upload_buffer->write_memory(0, data, sizeof(data));
	}

	SharedPtr<GPUTimestampPool> timestamp_pool = device->create_gpu_timestamp_pool(queue.get(), 1024);
	SharedPtr<GPUStatisticPool> stats_pool = device->create_gpu_statistic_pool(queue.get(), 32);

	SharedPtr<Memory> mem_uav = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024);
	SharedPtr<Buffer> bfr_uav = device->create_buffer(mem_uav, 64ull * 1024, BufferType::GeneralDataBuffer, true);
	UnorderedAccessView uav_uav = device->create_unordered_access_view(bfr_uav, sizeof(float) * 4, 1);
	ShaderResourceView srv_uav = device->create_shader_resource_view(bfr_uav, sizeof(float) * 4, 1);

	Ptr<sg::UploadHeap> frame_upload_heap(new sg::UploadHeap(device.get(), frame_count, 1024 * 1024 * 256));
	Ptr<SimpleLinearConstantBuffer> linear_cb(new SimpleLinearConstantBuffer(device, 1024 * 1024));

	Camera camera;
	camera.SetWidthHeight((float)w, (float)h);
	camera.SetPosition({ 0.0f, 0.25f, -2.0f });

	sg::Ptr<DebugDraw> debug_draw = sg::Ptr<DebugDraw>(new DebugDraw(*device));

	sg::SharedPtr<sg::Texture> dds_texture;

	BasicGrid basic_grid(device);
	Ptr<ModelViewer> model_viewer = Ptr<ModelViewer>(new ModelViewer(device));
	Ptr<MagnifyingGlass> magnifying_glass(new MagnifyingGlass(device));
	Ptr< PostProcess> post_process(new PostProcess(device));
	device->set_imgui_viewer_texture(magnifying_glass->render_texture);
	Ptr< Terrain> terrain = Ptr<Terrain>(new Terrain(device));
	//Ptr< BitonicSort> bitonic_sort = Ptr< BitonicSort>(new BitonicSort(device));
	volatile bool run = true;

	// I start this at value of 1 because the D3D12 fence doesn't like waiting on a value of 0...
	u32 total_frame_idx = 1;

	Viewport vp;
	vp.width = (u32)w;
	vp.height = (u32)h;
	ScissorRect sc;
	sc.right = (u32)w;
	sc.bottom = (u32)h;

	AverageTimer gpu_timer;
	AverageTimer cpu_timer;

	bool bOpen = true;
	float delta_time; // seconds
	float total_time; // seconds
	auto total_time_start = std::chrono::high_resolution_clock::now();
	auto last_frame_start = std::chrono::high_resolution_clock::now();
	D3D12_QUERY_DATA_PIPELINE_STATISTICS query_data = {};
	while (run)
	{
		auto cpu_start_time = std::chrono::high_resolution_clock::now();
		total_time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - total_time_start).count();
		delta_time = std::chrono::duration<float>(cpu_start_time - last_frame_start).count();
		last_frame_start = cpu_start_time;

		frame_upload_heap->begin_frame(queue);
		linear_cb->BeginFrame(frame_upload_heap.get());
		
		input->Update();
		
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		//ImPlot::ShowDemoWindow();
		//ImGui::ShowStyleEditor();

		static bool top_bar_once = true;
		if (top_bar_once)
		{
			// If we set this every frame, we need to be accurate to the screen pixel coordinates, by setting it only once, imgui keeps it tracked internally.
			ImGui::SetNextWindowPos(ImVec2(x_off, y_off));
			top_bar_once = false;
		}
		ImGui::SetNextWindowSize(ImVec2(w, 30));
		ImGui::SetNextWindowBgAlpha(0.5f);
		ImGui::Begin("Top Bar", nullptr, 			
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoSavedSettings | 
			ImGuiWindowFlags_NoCollapse | 
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration
#ifdef SE_IMGUI_DOCKING
			| ImGuiWindowFlags_NoDocking
#endif

		);
		ImGui::Text("CPU: %5.1fus", cpu_timer.get_average());
		ImGui::SameLine();
		ImGui::Text("GPU: %5.1fus", gpu_timer.get_average());
		ImGui::SameLine();
		ImGui::PlotHistogram("gpu", gpu_timer.get_history_buffer(), gpu_timer.get_history_count(), gpu_timer.get_history_idx());
		//static float 
		//ImPlot::PlotHistogram2D(,)
		//ImGui::PlotHistogram("GPU Time", )

		ImGui::End();

		ImGui::Begin("Slim Graphics", &bOpen, 0);

		device->imgui_update();

		// https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples
		
		debug_draw->Update();
		model_viewer->Update(delta_time, total_time, camera, *debug_draw);
		magnifying_glass->Update(wnd->g_hWnd, *input, delta_time, total_time, camera, *debug_draw);
		basic_grid.Update(delta_time, total_time, camera);
		post_process->Update(wnd->g_hWnd, *input, delta_time, total_time, camera, *debug_draw);

		camera.Update(delta_time, total_time, *input);
		if (ImGui::CollapsingHeader("Performance"))
		{
			// Plots can display overlay texts
			// (in this example, we will display an average value)
			{
				char overlay[32];
				sprintf_s(overlay, "CPU - %u", (u32)cpu_timer.most_recent());
				char text[128];
				sprintf_s(text, "Average: %u\nMin: %u\nMax: %u", (u32)cpu_timer.get_average(), (u32)cpu_timer.min(), (u32)cpu_timer.max());
				ImGui::PlotLines(text, cpu_timer.get_history_buffer(), cpu_timer.get_history_count(), cpu_timer.get_history_idx(), overlay, 0.0f, 50000.0f, ImVec2(0, 80.0f));
			}
			{
				char overlay[32];
				sprintf_s(overlay, "GPU - %u", (u32)gpu_timer.most_recent());
				char text[128];
				sprintf_s(text, "Average: %u\nMin: %u\nMax: %u", (u32)gpu_timer.get_average(), (u32)gpu_timer.min(), (u32)gpu_timer.max());
				ImGui::PlotLines(text, gpu_timer.get_history_buffer(), gpu_timer.get_history_count(), gpu_timer.get_history_idx(), overlay, 0.0f, 50000.0f, ImVec2(0, 80.0f));
			}

		}
		if (ImGui::CollapsingHeader("GPU Pipeline Stats"))
		{
			{ // Query Stats
				ImGui::Text("IAVertices:    %llu", query_data.IAVertices);
				ImGui::Text("IAPrimitives:  %llu", query_data.IAPrimitives);
				ImGui::Text("VSInvocations: %llu", query_data.VSInvocations);
				ImGui::Text("GSInvocations: %llu", query_data.GSInvocations);
				ImGui::Text("GSPrimitives:  %llu", query_data.GSPrimitives);
				ImGui::Text("CInvocations:  %llu", query_data.CInvocations);
				ImGui::Text("CPrimitives:   %llu", query_data.CPrimitives);
				ImGui::Text("PSInvocations: %llu", query_data.PSInvocations);
				ImGui::Text("HSInvocations: %llu", query_data.HSInvocations);
				ImGui::Text("DSInvocations: %llu", query_data.DSInvocations);
				ImGui::Text("CSInvocations: %llu", query_data.CSInvocations);
				//ImGui::Text("ASInvocations: %ull", query_data.ASInvocations);
				//ImGui::Text("MSInvocations: %ull", query_data.MSInvocations);
				//ImGui::Text("MSPrimitives:  %ull", query_data.MSPrimitives);
			}
		}
		ImGui::End();
	
		command_buffer->start_recording();
		timestamp_pool->begin_frame();
		stats_pool->begin_frame();
		GPUTimestampPool::Index gpu_timestamp_idx = timestamp_pool->allocate_new_timestamp();
		GPUStatisticPool::Index stat_query_idx = stats_pool->allocate_new_query();
		timestamp_pool->begin_timestamp(gpu_timestamp_idx, command_buffer.get());
		{

			//Compute Dispatch
			{
				Binding b;
				b.uav_binding_count = 1;
				b.set_uav(uav_uav, 0);
				command_buffer->set_pipeline(pipeline_compute.get());
				command_buffer->bind(b, PipelineType::Compute);
				command_buffer->dispatch();
			}

			// Bitonic Sort
			{
				//bitonic_sort->sort(*command_buffer, *linear_cb);
			}

			command_buffer->start_geometry_pass(1, &intermediate_rtv, vp, sc);
			{
				float4 colour = { 0.0f,1.0f,0.0f,1.0f };
				command_buffer->clear_render_target_view(intermediate_rtv, colour);
			}
			command_buffer->end_geometry_pass();

			command_buffer->start_geometry_pass(1, &final_rtv, vp, sc);
			{
				float4 colour = { 0.0f,1.0f,0.0f,1.0f };
				command_buffer->clear_render_target_view(final_rtv, colour);
			}
			command_buffer->clear_depth_stencil_view(dsv, true, false);
			{
				command_buffer->copy_buffer_to_buffer(cbfr.get(), upload_buffer.get());
			}
			{
				command_buffer->set_pipeline(pipeline.get());
				command_buffer->draw_instanced(6, 1, 0, 0);
			}
			{
				command_buffer->set_pipeline(pipeline_cb.get());
				Binding b;
				b.cbv_binding_count = 1;
				b.set_cbv(cbv, 0);
				command_buffer->bind(b, PipelineType::Geometry);
				command_buffer->draw_instanced(6, 1, 0, 0);
			}
			{
				command_buffer->set_pipeline(pipeline_b.get());
				Binding b;
				b.srv_binding_count = 1;
				b.set_srv(srv_uav, 0);
				command_buffer->bind(b, PipelineType::Geometry);
				command_buffer->draw_instanced(6, 1, 0, 0);
			}

			command_buffer->end_geometry_pass();

			sg::ConstantBufferView cbv_cam = linear_cb->AllocateAndWrite<ShaderStructs::CameraData>(camera.GetCameraShaderData());

			command_buffer->start_geometry_pass(1, &final_rtv, vp, sc, dsv);
			{ // Model Viewer
				stats_pool->begin_query(stat_query_idx, command_buffer.get());
				model_viewer->Render(*command_buffer, camera, cbv_cam, frame_upload_heap, *linear_cb, *debug_draw);
				terrain->Render(*command_buffer, camera, cbv_cam, frame_upload_heap, *linear_cb, *debug_draw);
				stats_pool->end_query(stat_query_idx, command_buffer.get());
				basic_grid.Render(*command_buffer, camera, cbv_cam, frame_upload_heap, *linear_cb);
				debug_draw->Render(*command_buffer, cbv_cam);
			}
			command_buffer->end_geometry_pass();

			SharedPtr<Texture> copy_to_swap_chain_target = final_render_target;
			ShaderResourceView copy_to_swap_chain_target_view = final_srv;
			// Post Process
			{
			
				if (post_process->Render(*command_buffer, cbv_cam, intermediate_uav, final_srv, depth_srv, *linear_cb, w, h))
				{
					copy_to_swap_chain_target = intermediate_render_target;
					copy_to_swap_chain_target_view = intermediate_srv;
				}
			}

			command_buffer->copy_texture_to_texture(*(backbuffer_rtvs[current_frame_idx]->texture_resource), *copy_to_swap_chain_target);

			// Magnify
			{
				magnifying_glass->Render(*command_buffer, cbv_cam, copy_to_swap_chain_target_view, *linear_cb);
			}

			command_buffer->start_geometry_pass(1, &backbuffer_rtvs[current_frame_idx], vp, sc);
			{
				ImGui::Render();
				device->imgui_render(command_buffer.get());
			}
			command_buffer->end_geometry_pass();
		}


		timestamp_pool->end_timestamp(gpu_timestamp_idx, command_buffer.get());
		timestamp_pool->end_frame(command_buffer.get());
		stats_pool->end_frame(command_buffer.get());
		command_buffer->end_recording();

		linear_cb->EndFrame();
		frame_upload_heap->end_frame();
		
		queue->submit_command_list(command_buffer.get());
		current_frame_idx = device->present_swap_chain(queue.get());
		queue->fence_signal(fence.Get(), total_frame_idx);
		queue->fence_wait_gpu(fence.Get(), total_frame_idx);
		queue->fence_wait_cpu(fence.Get(), total_frame_idx);

		//Timestamps
		timestamp_pool->resolve();
		gpu_timer.add_time(timestamp_pool->collect_timestamp_us(gpu_timestamp_idx));

		stats_pool->resolve();
		query_data = stats_pool->collect_query(stat_query_idx);

		total_frame_idx++;
		wnd->Poll();

		cpu_timer.add_time(std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - cpu_start_time).count() * 1000000.0);
	}
}