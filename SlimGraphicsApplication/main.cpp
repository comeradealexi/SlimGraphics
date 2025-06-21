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

//Application headers
#include "Model.h"
#include "UploadHeap.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "ModelViewer.h"
#include "DebugDraw.h"

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
	se::Ptr<se::Window> wnd(new se::Window(hInstance, nCmdShow, w,h));
	{
		RECT cr;
		GetClientRect(wnd->g_hWnd, &cr);
		w = cr.right;
		h = cr.bottom;
	}
	se::Ptr<se::GameInput> input(new se::GameInput());

	SharedPtr<Device> device(new Device());
	Ptr<CommandQueue> queue = device->create_command_queue();
	Ptr<CommandList> command_buffer = device->create_command_buffer();
	auto fence = device->create_queue_fence();

	SharedPtr<Memory> mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024, 64ull * 1024);
	mem = nullptr;

	//ImGui
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImPlot::CreateContext();
		ImGui::StyleColorsDark();
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
	Ptr<VertexShader> vs;
	Ptr<PixelShader> ps;
	Ptr<PixelShader> ps_cb;
	Ptr<PixelShader> ps_b;
	Ptr<ComputeShader> cs;
	{
		std::vector<uint8_t> vertex_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\Basic_VertexShader.PC_DXC");
		std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\Basic_PixelShader.PC_DXC");
		vs = device->create_vertex_shader(vertex_data);
		ps = device->create_pixel_shader(pixel_data);
	}
	{
		std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\Basic_ConstantBuffer_PixelShader.PC_DXC");
		ps_cb = device->create_pixel_shader(pixel_data);
	}
	{
		std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\Basic_Buffer_PixelShader.PC_DXC");
		ps_b = device->create_pixel_shader(pixel_data);
	}
	{
		std::vector<uint8_t> compute_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\Basic_ComputeShader.PC_DXC");
		cs = device->create_compute_shader(compute_data);
	}

	RenderTargetView rtvs[frame_count];
	u32 current_frame_idx = device->create_swap_chain(wnd->g_hWnd, queue.get(), frame_count, back_buffer_format, w, h, rtvs);

	// Depth buffer
	DepthStencilView dsv;
	SharedPtr<Memory> dsv_mem;
	SharedPtr<Texture> dsv_tex;
	{
		ResourceCreateDesc rd;
		rd.width = w;
		rd.height = h;
		rd.format = DXGI_FORMAT_D32_FLOAT;
		rd.usage_flags = ResourceUsageFlags::DepthStencil;
		SizeAndAlignment sal = device->calculate_resource_size_alignment(rd);
		dsv_mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Target, sal.size, sal.alignment);
		dsv_tex = device->create_texture(dsv_mem, sal.size, sal.alignment, rd);
		dsv = device->create_depth_stencil_view(dsv_tex);
	}

	//Pipeline
	Ptr<Pipeline> pipeline;
	Ptr<Pipeline> pipeline_cb;
	Ptr<Pipeline> pipeline_b;
	Ptr<Pipeline> pipeline_compute;
	{
		BindingDesc bd;
		//bd.cbv_binding_count = 1;

		PipelineDesc::Graphics desc;
		desc.pixel_shader = ps.get();
		desc.vertex_shader = vs.get();
		desc.render_target_count = 1;
		desc.render_target_format_list[0] = back_buffer_format;
		desc.depth_stencil_desc.depth_enable = false;
		desc.depth_stencil_desc.depth_write = false;
		pipeline = device->create_pipeline(desc, bd);

		bd.cbv_binding_count = 1;
		desc.pixel_shader = ps_cb.get();
		pipeline_cb = device->create_pipeline(desc, bd);

		bd.cbv_binding_count = 0;
		bd.srv_binding_count = 1;
		desc.pixel_shader = ps_b.get();
		pipeline_b = device->create_pipeline(desc, bd);
	}
	{
		BindingDesc bd;
		bd.uav_binding_count = 1;
		PipelineDesc::Compute desc;
		desc.compute_shader = cs.get();
		pipeline_compute = device->create_pipeline(desc, bd);
	}

	//CB
	mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024, 64ull * 1024);
	SharedPtr<Buffer> cbfr = device->create_buffer(mem, 64ull * 1024, 64ull * 1024, BufferType::Constant, false);
	ConstantBufferView cbv = device->create_constant_buffer_view(cbfr.get(), 0, 256);

	SharedPtr<Memory> upload_heap = device->allocate_memory(MemoryType::Upload, MemorySubType::None, 64ull * 1024, 64ull * 1024);
	SharedPtr<Buffer> upload_buffer = device->create_buffer(upload_heap, 64ull * 1024, 64ull * 1024, BufferType::Upload, false);
	{
		float data[4] = { 1.0f,0.0f,0.0f,1.0f };
		upload_buffer->write_memory(0, data, sizeof(data));
	}

	Ptr<GPUTimestampPool> timestamp_pool = device->create_gpu_timestamp_pool(queue.get(), 1024);
	Ptr<GPUStatisticPool> stats_pool = device->create_gpu_statistic_pool(queue.get(), 32);

	SharedPtr<Memory> mem_uav = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024, 64ull * 1024);
	SharedPtr<Buffer> bfr_uav = device->create_buffer(mem_uav, 64ull * 1024, 64ull * 1024, BufferType::GeneralDataBuffer, true);
	UnorderedAccessView uav_uav = device->create_unordered_access_view(bfr_uav, sizeof(float) * 4, 1);
	ShaderResourceView srv_uav = device->create_shader_resource_view(bfr_uav, sizeof(float) * 4, 1);

	Ptr<UploadHeap> frame_upload_heap(new UploadHeap(device.get(), frame_count, 1024 * 1024 * 32));
	Ptr<SimpleLinearConstantBuffer> linear_cb(new SimpleLinearConstantBuffer(device, 1024 * 1024));

	Ptr<Model> model;

	Camera camera;
	camera.SetWidthHeight((float)w, (float)h);
	camera.SetPosition({ 0.0f, 0.5f, -2.0f });

	sg::Ptr<DebugDraw> debug_draw = sg::Ptr<DebugDraw>(new DebugDraw(*device));

	//Loading frame
	Ptr<VertexShader> model_vs;
	Ptr<PixelShader> model_ps;
	Ptr<Pipeline> model_pipeline;
	SharedPtr<Memory> model_cb_mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024, 64ull * 1024);
	SharedPtr<Buffer> model_cb = device->create_buffer(model_cb_mem, 64ull * 1024, 64ull * 1024, BufferType::Constant, false);
	ConstantBufferView model_cbv = device->create_constant_buffer_view(model_cb.get(), 0, 256);
	{
		frame_upload_heap->begin_frame(queue.get());
		Model::InitData mode_initdata;
		mode_initdata.file_path = "../SlimGraphicsAssets/DebugModels/teapot.obj";
		model = Ptr<Model>(new Model(device.get(), frame_upload_heap.get(), mode_initdata));
		frame_upload_heap->end_frame(queue.get());

		// Make model pipeline
		std::vector<uint8_t> vertex_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\Basic3D_VertexShader.PC_DXC");
		std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\Basic3D_PixelShader.PC_DXC");
		model_vs = device->create_vertex_shader(vertex_data);
		model_ps = device->create_pixel_shader(pixel_data);
		
		BindingDesc bd;
		bd.cbv_binding_count = 2;

		PipelineDesc::Graphics desc;
		desc.input_layout = Model::Vertex::make_input_layout();
		desc.pixel_shader = model_ps.get();
		desc.vertex_shader = model_vs.get();
		desc.render_target_count = 1;
		desc.render_target_format_list[0] = back_buffer_format;
		desc.depth_stencil_desc.depth_enable = true;
		desc.depth_stencil_desc.depth_write = true;
		model_pipeline = device->create_pipeline(desc, bd);
		model->SetPipeline(std::move(model_pipeline));
	}

	BasicGrid basic_grid(device);
	Ptr<ModelViewer> model_viewer = Ptr<ModelViewer>(new ModelViewer(device));

	volatile bool run = true;

	u32 total_frame_idx = 0;

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

		frame_upload_heap->begin_frame(queue.get());
		linear_cb->BeginFrame(frame_upload_heap.get());
		
		input->Update();
		
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImPlot::ShowDemoWindow();
		ImGui::ShowStyleEditor();

		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(w, 60));
		ImGui::SetNextWindowBgAlpha(0.5f);
		ImGui::Begin("Top Bar", nullptr, 			
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoSavedSettings | 
			ImGuiWindowFlags_NoCollapse | 
			ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::Text("Test 1");
		ImGui::SameLine();
		ImGui::Text("Test 2");
		ImGui::SameLine();
		ImGui::PlotHistogram("gpu", gpu_timer.get_history_buffer(), gpu_timer.get_history_count(), gpu_timer.get_history_idx());
		//static float 
		//ImPlot::PlotHistogram2D(,)
		//ImGui::PlotHistogram("GPU Time", )
		
		ImGui::End();

		ImGui::Begin("Slim Graphics", &bOpen, 0);

		model_viewer->Update(delta_time, total_time, camera, *debug_draw);

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
			{
#if 0
				if (ImPlot::BeginPlot("##Rolling", ImVec2(0.0f, 150.0f))) 
				{
					ImPlot::SetupAxes(nullptr, "us", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
					ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, cpu_timer.get_history_count(), ImGuiCond_Always);
					ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 50000.0);
					ImPlot::PlotLine<float>("CPU", cpu_timer.get_history_buffer(), (int)cpu_timer.get_history_idx());
					ImPlot::PlotLine<float>("GPU", gpu_timer.get_history_buffer(), (int)gpu_timer.get_history_idx());

					ImPlot::EndPlot();
				}
#endif
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

			command_buffer->start_geometry_pass(1, &rtvs[current_frame_idx], vp, sc, true);
			{
				float4 colour = { 0.0f,1.0f,0.0f,1.0f };
				command_buffer->clear_render_target_view(rtvs[current_frame_idx], colour);
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
			{ // Model
#if false
				sg::ConstantBufferView cbv_cam = linear_cb->AllocateAndWrite<ShaderStructs::CameraData>(camera.GetCameraShaderData());

				ShaderStructs::ModelData model_data;
				model_data.model_matrix = DirectX::XMMatrixIdentity();
				sg::ConstantBufferView cbv_model = linear_cb->AllocateAndWrite<ShaderStructs::ModelData>(model_data);

				model->Render(command_buffer.get(), cbv_cam, cbv_model);
#endif
			}
			command_buffer->end_geometry_pass();

			command_buffer->start_geometry_pass(1, &rtvs[current_frame_idx], vp, sc, true, &dsv);
			{ // Model Viewer
				sg::ConstantBufferView cbv_cam = linear_cb->AllocateAndWrite<ShaderStructs::CameraData>(camera.GetCameraShaderData());

				stats_pool->begin_query(stat_query_idx, command_buffer.get());
				model_viewer->Render(*command_buffer, camera, cbv_cam, frame_upload_heap, *linear_cb, *debug_draw);
				stats_pool->end_query(stat_query_idx, command_buffer.get());
				basic_grid.Render(*command_buffer, camera, cbv_cam, frame_upload_heap, *linear_cb);
				debug_draw->Render(*command_buffer, cbv_cam);
			}
			command_buffer->end_geometry_pass();

			command_buffer->start_geometry_pass(1, &rtvs[current_frame_idx], vp, sc, true);
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
		frame_upload_heap->end_frame(queue.get());
		
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