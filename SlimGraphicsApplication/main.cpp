#include <sgPlatformInclude.h>

using namespace sg;
int main()
{
	SharedPtr<Device> device(new Device());
	Ptr<CommandQueue> queue = device->create_command_queue();
	Ptr<CommandList> command_buffer = device->create_command_buffer();

	Ptr<Memory> mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024, 64ull * 1024);
	mem = nullptr;
}