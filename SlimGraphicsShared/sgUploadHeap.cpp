#include "sgUploadHeap.h"
#include <sgUtils.h>

using namespace sg;

UploadHeap::UploadHeap(sg::Device* device, sg::u32 buffer_count, sg::u32 size_per_frame)
{
	per_frame_list.resize(buffer_count);
	for (u32 i = 0; i < buffer_count; i++)
	{
		PerFrameData& data = per_frame_list[i];
		SharedPtr<Memory> upload_heap = device->allocate_memory(MemoryType::Upload, MemorySubType::None, size_per_frame);
		data.buffer = device->create_buffer(upload_heap, size_per_frame,BufferType::Upload, false);
		data.command_list = device->create_command_buffer();
		data.fence = device->create_queue_fence();
		data.upload_heap_size = size_per_frame;
	}
}

void UploadHeap::begin_frame(sg::SharedPtr<sg::CommandQueue> queue)
{
	PerFrameData& data = frame_data();
	data.queue_ref = queue;
	data.upload_heap_offset = 0;
	
	if (data.fence_signalled)
	{
		queue->fence_wait_cpu(*data.fence, data.fence_wait_value);
	}
	data.command_list->start_recording();
	data.mapped_gpu_memory = data.buffer->map_memory();
}

UploadHeap::Offset UploadHeap::allocate_upload_memory(sg::u32 size, sg::u32 alignment)
{
	UploadHeap::Offset return_offset = UploadHeap::INVALID_OFFSET;

	PerFrameData& data = frame_data();
	Offset new_offset = AlignUp(data.upload_heap_offset, alignment);

	if ((new_offset + size) <= data.upload_heap_size)
	{
		return_offset = new_offset;
		data.upload_heap_offset = new_offset + size;
	}
	else
	{
		// Flush pending uploads and start new frame
		seAssert(false, "FAILURE ran out of upload space and this code isn't working yet!!");
		//seWriteLine("UploadHeap ran out of space! Flushing and starting new frame");
		//SharedPtr<CommandQueue> queue = data.queue_ref;
		//end_frame();
		//begin_frame(queue);
		//return_offset = 0;
	}

	return return_offset;
}

void UploadHeap::write_upload_memory(UploadHeap::Offset offset, const void* memory, sg::u32 size)
{
	seAssert(memory != nullptr, "expected valid ptr");
	seAssert(size > 0, "expected valid size");
	seAssert(offset != UploadHeap::INVALID_OFFSET, "invalid offset");

	PerFrameData& data = frame_data();

	memcpy(static_cast<char*>(data.mapped_gpu_memory) + offset, memory, size);
	//data.buffer->write_memory(offset, memory, size);
}

void UploadHeap::upload_to_buffer(sg::Buffer* dest_buffer, sg::u32 dest_byte_offset, Offset upload_heap_offset, sg::u32 size)
{
	PerFrameData& data = frame_data();
	data.command_list->copy_buffer_to_buffer(size, dest_buffer, dest_byte_offset, data.buffer.get(), upload_heap_offset);
}


void UploadHeap::upload_to_texture(sg::Texture* dest_texture, Offset upload_heap_offset, sg::u32 size)
{
	PerFrameData& data = frame_data();
	data.command_list->copy_buffer_to_texture(size, dest_texture, data.buffer.get(), upload_heap_offset);

}

void UploadHeap::upload_to_texture(sg::Texture* dest_texture, sg::u32 texture_mip_index, Offset upload_heap_offset, sg::u32 size)
{
	PerFrameData& data = frame_data();
	data.command_list->copy_buffer_to_texture(size, dest_texture, data.buffer.get(), upload_heap_offset, texture_mip_index);
}

void UploadHeap::upload_texture_subresource(sg::Texture* dest_texture, sg::u32 subresource_index, const void* data, sg::u32 tight_row_pitch, sg::u32 row_count)
{
	static constexpr u32 kTextureRowPitchAlignment = 256;	// D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
	static constexpr u32 kTexturePlacementAlignment = 512;	// D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT

	seAssert(dest_texture && data, "expected valid texture and data");
	seAssert(tight_row_pitch > 0 && row_count > 0, "expected non-empty texture data");

	const u32 aligned_row_pitch = AlignUp(tight_row_pitch, kTextureRowPitchAlignment);
	const u32 total_size = aligned_row_pitch * row_count;

	const Offset offset = allocate_upload_memory(total_size, kTexturePlacementAlignment);
	if (offset == INVALID_OFFSET)
	{
		return;
	}

	PerFrameData& frame = frame_data();
	char* dest = static_cast<char*>(frame.mapped_gpu_memory) + offset;
	const char* src = static_cast<const char*>(data);
	if (aligned_row_pitch == tight_row_pitch)
	{
		memcpy(dest, src, total_size);
	}
	else
	{
		for (u32 row = 0; row < row_count; row++)
		{
			memcpy(dest + (size_t)row * aligned_row_pitch, src + (size_t)row * tight_row_pitch, tight_row_pitch);
		}
	}

	frame.command_list->copy_buffer_to_texture(total_size, dest_texture, frame.buffer.get(), offset, subresource_index);
}

void UploadHeap::end_frame()
{
	PerFrameData& data = frame_data();

	data.mapped_gpu_memory = nullptr;
	data.buffer->unmap_memory();

	data.command_list->end_recording();
	data.queue_ref->submit_command_list(data.command_list.get());
	data.queue_ref->fence_signal(*data.fence, counter);
	data.fence_wait_value = counter;
	data.fence_signalled = true;
	data.queue_ref = nullptr;

	counter++;
}
