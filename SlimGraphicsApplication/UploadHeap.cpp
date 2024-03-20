#include "UploadHeap.h"

using namespace sg;

template <typename T>
static inline constexpr T AlignUp(T val, T align)
{
	return (val + align - 1) / align * align;
}

UploadHeap::UploadHeap(sg::Device* device, sg::u32 buffer_count, sg::u32 size_per_frame)
{
	per_frame_list.resize(buffer_count);
	for (u32 i = 0; i < buffer_count; i++)
	{
		PerFrameData& data = per_frame_list[i];
		SharedPtr<Memory> upload_heap = device->allocate_memory(MemoryType::Upload, MemorySubType::None, size_per_frame, 64ull * 1024);
		data.buffer = device->create_buffer(upload_heap, size_per_frame, 64ull * 1024, BufferType::Upload, false);
		data.command_list = device->create_command_buffer();
		data.fence = device->create_queue_fence();
		data.upload_heap_size = size_per_frame;
	}
}

void UploadHeap::begin_frame(CommandQueue* queue)
{
	counter++;
	PerFrameData& data = frame_data();
	
	data.upload_heap_offset = 0;
	
	if (data.fence_signalled)
	{
		queue->fence_wait_cpu(data.fence.Get(), data.fence_wait_value);
	}
	data.command_list->start_recording();
}

UploadHeap::Offset UploadHeap::allocate_upload_memory(sg::u32 size, sg::u32 alignment)
{
	UploadHeap::Offset return_offset = UploadHeap::INVALID_OFFSET;

	PerFrameData& data = frame_data();
	Offset new_offset = AlignUp(data.upload_heap_offset, alignment);
	new_offset += size;

	if (new_offset <= data.upload_heap_size)
	{
		return_offset = data.upload_heap_offset;
		data.upload_heap_offset = new_offset;
	}

	return return_offset;
}

void UploadHeap::write_upload_memory(UploadHeap::Offset offset, const void* memory, sg::u32 size)
{
	seAssert(memory != nullptr, "expected valid ptr");
	seAssert(size > 0, "expected valid size");
	seAssert(offset != UploadHeap::INVALID_OFFSET, "invalid offset");

	PerFrameData& data = frame_data();
	data.buffer->write_memory(offset, memory, size);
}

void UploadHeap::upload_to_buffer(sg::Buffer* dest_buffer, sg::u32 dest_byte_offset, Offset upload_heap_offset, sg::u32 size)
{
	PerFrameData& data = frame_data();
}

void UploadHeap::end_frame(CommandQueue* queue)
{
	PerFrameData& data = frame_data();

	data.command_list->end_recording();
	queue->submit_command_list(data.command_list.get());
	queue->fence_signal(data.fence.Get(), counter);
	data.fence_signalled = true;
}
