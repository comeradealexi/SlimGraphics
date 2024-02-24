#include <sgPlatformInclude.h>
#include <seEngineBasicFileIO.h>
#include <Win32/seWindow.h>
#include <imgui.h>
#include <implot.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include "AverageTimer.h"

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
		device->imgui_init(frame_count, back_buffer_format);
	}
	
	//Load Shaders
	Ptr<VertexShader> vs;
	Ptr<PixelShader> ps;
	Ptr<PixelShader> ps_cb;
	{
		std::vector<uint8_t> vertex_data = se::BasicFileIO::LoadFile("ShadersD3D12\\Debug\\Basic_VertexShader.cso");
		std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShadersD3D12\\Debug\\Basic_PixelShader.cso");
		vs = device->create_vertex_shader(vertex_data);
		ps = device->create_pixel_shader(pixel_data);
	}
	{
		std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShadersD3D12\\Debug\\Basic_ConstantBuffer_PixelShader.cso");
		ps_cb = device->create_pixel_shader(pixel_data);
	}

	RenderTargetView rtvs[frame_count];
	u32 current_frame_idx = device->create_swap_chain(wnd->g_hWnd, queue.get(), frame_count, back_buffer_format, w, h, rtvs);

	//Pipeline
	Ptr<Pipeline> pipeline;
	Ptr<Pipeline> pipeline_cb;
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
	}

	//CB
	mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024, 64ull * 1024);
	Ptr<Buffer> cbfr = device->create_buffer(mem, 64ull * 1024, 64ull * 1024, BufferType::Constant);
	ConstantBufferView cbv = device->create_constant_buffer_view(cbfr.get(), 0, 256);

	SharedPtr<Memory> upload_heap = device->allocate_memory(MemoryType::Upload, MemorySubType::None, 64ull * 1024, 64ull * 1024);
	Ptr<Buffer> upload_buffer = device->create_buffer(upload_heap, 64ull * 1024, 64ull * 1024, BufferType::Upload);
	{
		float data[4] = { 1.0f,0.0f,0.0f,1.0f };
		upload_buffer->write_memory(0, data, sizeof(data));
	}

	Ptr<GPUTimestampPool> timestamp_pool = device->create_gpu_timestamp_pool(queue.get(), 1024);

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
	while (run)
	{
		auto cpu_start_time = std::chrono::high_resolution_clock::now();
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImPlot::ShowDemoWindow();
		ImGui::Begin("Slim Graphics", &bOpen, 0);
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

		//if (bOpen) { ImGui::ShowDemoWindow(&show_demo_window); }

		command_buffer->start_recording();
		timestamp_pool->begin_frame();
		GPUTimestampPool::Index gpu_timestamp_idx = timestamp_pool->allocate_new_timestamp();
		timestamp_pool->begin_timestamp(gpu_timestamp_idx, command_buffer.get());
		{
			command_buffer->start_render_pass(1, &rtvs[current_frame_idx], vp, sc, true);
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
				b.set_cbv(cbv.cbv, 0);
				command_buffer->bind(b);
				command_buffer->draw_instanced(6, 1, 0, 0);
			}
			{
				ImGui::End();
				ImGui::Render();
				device->imgui_render(command_buffer.get());
			}
			command_buffer->end_render_pass();
		}
		timestamp_pool->end_timestamp(gpu_timestamp_idx, command_buffer.get());
		timestamp_pool->end_frame(command_buffer.get());
		command_buffer->end_recording();

		queue->submit_command_list(command_buffer.get());
		current_frame_idx = device->present_swap_chain(queue.get());
		queue->fence_signal(fence.Get(), total_frame_idx);
		queue->fence_wait_gpu(fence.Get(), total_frame_idx);
		queue->fence_wait_cpu(fence.Get(), total_frame_idx);

		//Timestamps
		timestamp_pool->resolve();
		gpu_timer.add_time(timestamp_pool->collect_timestamp_us(gpu_timestamp_idx));
		total_frame_idx++;
		wnd->Poll();

		cpu_timer.add_time(std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - cpu_start_time).count() * 1000000.0);
	}
}