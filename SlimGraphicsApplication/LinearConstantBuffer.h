#pragma once
#include <sgPlatformInclude.h>
#include <sgTypes.h>
#include <vector>
#include "sgUploadHeap.h"

// This constant buffer manager is very simple because it uploads a fixed amount of memory and it cannot auto-expand in size.
class SimpleLinearConstantBuffer
{
public:
	SimpleLinearConstantBuffer(sg::SharedPtr<sg::Device> _device, sg::u32 size);

	void BeginFrame(sg::UploadHeap* upload_heap);
	void EndFrame();
	sg::ConstantBufferView Allocate(sg::u32 size);
	sg::ConstantBufferView AllocateAndWrite(const void* mem, sg::u32 size);

	template <typename T>
	sg::ConstantBufferView AllocateAndWrite(const T& var)
	{
		return AllocateAndWrite((void*)&var, sizeof(T));
	}

private:
	const sg::u32 max_size;
	sg::u32 current_offset = 0;
	sg::UploadHeap* current_upload_heap = nullptr;
	sg::UploadHeap::Offset current_upload_heap_start_offset = 0;
	sg::SharedPtr<sg::Buffer> constant_buffer;
	sg::SharedPtr<sg::Device> device;
};

