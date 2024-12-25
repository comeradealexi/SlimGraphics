#pragma once
#include <sgPlatformInclude.h>
#include <sgTypes.h>
#include <vector>

class LinearConstantBuffer
{
public:
	LinearConstantBuffer(sg::SharedPtr<sg::Device> _device, sg::SharedPtr<sg::Buffer> _constant_buffer, sg::u32 offset, sg::u32 size);

	void Reset();
	sg::ConstantBufferView Allocate(sg::u32 size);

private:
	const sg::u32 base_offset;
	const sg::u32 max_size;
	sg::u32 current_offset = 0;
	sg::SharedPtr<sg::Buffer> constant_buffer;
	sg::SharedPtr<sg::Device> device;
};

