#include <sgPlatformInclude.h>
#include <seEngineBasicFileIO.h>
#include <Win32/seWindow.h>

//TODO
//Create swap chain
//Create window
//Create PSO
//Create command buffer
//Command buffer submission
//Swap chain presentation

using namespace sg;
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	size_t w = 1920;
	size_t h = 1080;
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

	//Pipeline
	Pipeline::GraphicsDesc gdesc;
	gdesc.pixel_shader = ps.get();
	gdesc.vertex_shader = vs.get();

	
}