#include "UploadHeap.h"

using namespace sg;

UploadHeap::UploadHeap(sg::Device* device, sg::u32 buffer_count, sg::u32 size_per_frame)
{
	command_buffer = device->create_command_buffer();

	buffer_list.resize(buffer_count);
	for (u32 i = 0; i < buffer_count; i++)
	{
		SharedPtr<Memory> upload_heap = device->allocate_memory(MemoryType::Upload, MemorySubType::None, size_per_frame, 64ull * 1024);
		buffer_list[i] = device->create_buffer(upload_heap, size_per_frame, 64ull * 1024, BufferType::Upload, false);
	}
}
