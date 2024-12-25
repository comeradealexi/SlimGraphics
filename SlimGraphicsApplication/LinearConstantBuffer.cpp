#include "LinearConstantBuffer.h"
#include <sgUtils.h>

LinearConstantBuffer::LinearConstantBuffer(sg::SharedPtr<sg::Device> _device, sg::SharedPtr<sg::Buffer> _constant_buffer, sg::u32 offset, sg::u32 size)  : base_offset(offset), max_size(size)
{
	device = _device;
	constant_buffer = _constant_buffer;
	seAssert(offset % sg::DefaultAlignment::CONSTANT_BUFFER_ALIGNMENT == 0, "Alignment not suitable");
}

void LinearConstantBuffer::Reset()
{
	current_offset = 0;
}

sg::ConstantBufferView LinearConstantBuffer::Allocate(sg::u32 size)
{
	size = sg::AlignUp(size, (sg::u32)sg::DefaultAlignment::CONSTANT_BUFFER_ALIGNMENT);
	if (current_offset + size >= max_size)
	{
		seAssert(false, "Linear constant buffer cannot allocate anymore.");
		return {};
	}
	sg::ConstantBufferView cbv = device->create_constant_buffer_view(constant_buffer.get(), base_offset + current_offset, size);
	current_offset += size;
	return cbv;
}
