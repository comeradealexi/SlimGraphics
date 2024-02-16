#include <sgPlatformInclude.h>
#include <seEngineBasicFileIO.h>
#include <Win32/seWindow.h>

//TODO
//Create PSO
//Create command buffer
//Command buffer submission
//Swap chain presentation

using namespace sg;
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	const u64 w = 1920;
	const u64 h = 1080;
	se::Ptr<se::Window> wnd(new se::Window(hInstance, nCmdShow, w,h));

	SharedPtr<Device> device(new Device());
	Ptr<CommandQueue> queue = device->create_command_queue();
	Ptr<CommandList> command_buffer = device->create_command_buffer();

	Ptr<Memory> mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024, 64ull * 1024);
	mem = nullptr;
	
	//Load Shadeers
	Ptr<VertexShader> vs;
	Ptr<PixelShader> ps;
	{
		std::vector<uint8_t> vertex_data = se::BasicFileIO::LoadFile("ShadersD3D12\\Debug\\Basic_VertexShader.cso");
		std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShadersD3D12\\Debug\\Basic_PixelShader.cso");
		vs = device->create_vertex_shader(vertex_data.data(), vertex_data.size());
		ps = device->create_pixel_shader(pixel_data.data(), pixel_data.size());
	}

	const u32 frame_count = 3;
	RenderTargetView rtvs[frame_count];
	device->create_swap_chain(wnd->g_hWnd, queue.get(), frame_count, DXGI_FORMAT_R8G8B8A8_UNORM, w, h, rtvs);

	//Pipeline
	Ptr<Pipeline> pipeline;
	{
		BindingDesc bd;

		PipelineDesc::Graphics desc;
		desc.pixel_shader = ps.get();
		desc.vertex_shader = vs.get();
		desc.render_target_count = 1;
		desc.render_target_format_list[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pipeline = device->create_pipeline(desc);
	}

	volatile bool run = true;
	while (run)
	{
		wnd->Poll();
	}
}