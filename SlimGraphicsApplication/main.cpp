#include <sgPlatformInclude.h>

using namespace sg;
int main()
{
	SharedPtr<Device> device(new Device());
	Ptr<CommandQueue> queue = device->create_command_queue();
	Ptr<CommandList> command_buffer = device->create_command_buffer();
}