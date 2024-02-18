#include <sgPlatformInclude.h>
#include <seEngineBasicFileIO.h>
#include <Win32/seWindow.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

//TODO
//Create PSO
//Create command buffer
//Command buffer submission
//Swap chain presentation

using namespace sg;
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	const DXGI_FORMAT back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;
	const u32 frame_count = 3;
	const u64 w = 1920;
	const u64 h = 1080;
	se::Ptr<se::Window> wnd(new se::Window(hInstance, nCmdShow, w,h));

	SharedPtr<Device> device(new Device());
	Ptr<CommandQueue> queue = device->create_command_queue();
	Ptr<CommandList> command_buffer = device->create_command_buffer();
	auto fence = device->create_queue_fence();

	Ptr<Memory> mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024, 64ull * 1024);
	mem = nullptr;

	//ImGui
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();
		ImGui_ImplWin32_Init(wnd->g_hWnd);
		ShowWindow(wnd->g_hWnd, SW_SHOWNORMAL);
		device->imgui_init(frame_count, back_buffer_format);
	}
	
	//Load Shaders
	Ptr<VertexShader> vs;
	Ptr<PixelShader> ps;
	{
		std::vector<uint8_t> vertex_data = se::BasicFileIO::LoadFile("ShadersD3D12\\Debug\\Basic_VertexShader.cso");
		std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShadersD3D12\\Debug\\Basic_PixelShader.cso");
		vs = device->create_vertex_shader(vertex_data);
		ps = device->create_pixel_shader(pixel_data);
	}

	RenderTargetView rtvs[frame_count];
	u32 current_frame_idx = device->create_swap_chain(wnd->g_hWnd, queue.get(), frame_count, back_buffer_format, w, h, rtvs);

	//Pipeline
	Ptr<Pipeline> pipeline;
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
	}

	volatile bool run = true;

	u32 total_frame_idx = 0;

	Viewport vp;
	vp.width = w;
	vp.height = h;
	ScissorRect sc;
	sc.right = w;
	sc.bottom = h;

	bool show_demo_window = true;
	bool bOpen = true;
	while (run)
	{
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGui::Begin("Dear ImGui Demo", &bOpen, 0);
		ImGui::ShowDemoWindow(&show_demo_window);
		command_buffer->start_recording();
		{
			command_buffer->start_render_pass(1, &rtvs[current_frame_idx], vp, sc, true);
			{
				command_buffer->set_pipeline(pipeline.get());
				command_buffer->draw_instanced(6, 1, 0, 0);
			}
			{
				ImGui::End();
				ImGui::Render();
				device->imgui_render(command_buffer.get());
			}
			command_buffer->end_render_pass();
		}

		command_buffer->end_recording();

		queue->submit_command_list(command_buffer.get());
		current_frame_idx = device->present_swap_chain(queue.get());
		queue->fence_signal(fence.Get(), total_frame_idx);
		queue->fence_wait(fence.Get(), total_frame_idx);

		total_frame_idx++;
		wnd->Poll();
	}
}