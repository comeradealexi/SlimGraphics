#include "LinearConstantBuffer.h"
#include <sgUtils.h>

using namespace sg;

SimpleLinearConstantBuffer::SimpleLinearConstantBuffer(sg::SharedPtr<sg::Device> _device, sg::u32 size) : max_size(size)
{
	device = _device;
	
	SharedPtr<Memory> mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, size);
	constant_buffer = device->create_buffer(mem, size, BufferType::Constant, false);
}

void SimpleLinearConstantBuffer::BeginFrame(UploadHeap* upload_heap)
{
	current_offset = 0;
	current_upload_heap = upload_heap;
	current_upload_heap_start_offset = current_upload_heap->allocate_upload_memory(max_size, 64ull * 1024ull);
	current_upload_heap->upload_to_buffer(constant_buffer.get(), 0, current_upload_heap_start_offset, max_size);
}

void SimpleLinearConstantBuffer::EndFrame()
{
	current_upload_heap = nullptr;
	current_upload_heap_start_offset = 0;
}

sg::ConstantBufferView SimpleLinearConstantBuffer::Allocate(sg::u32 size)
{
	size = sg::AlignUp(size, (sg::u32)sg::DefaultAlignment::CONSTANT_BUFFER_ALIGNMENT);
	if (current_offset + size >= max_size)
	{
		seAssert(false, "Linear constant buffer cannot allocate anymore.");
		return {};
	}
	sg::ConstantBufferView cbv = device->create_constant_buffer_view(constant_buffer.get(), current_offset, size);
	current_offset += size;
	return cbv;
}

sg::ConstantBufferView SimpleLinearConstantBuffer::AllocateAndWrite(const void* mem, sg::u32 size)
{
	sg::u32 write_offet = current_offset;
	sg::ConstantBufferView cbv = Allocate(size);
	current_upload_heap->write_upload_memory(current_upload_heap_start_offset + write_offet, mem, size);
	return cbv;
}
