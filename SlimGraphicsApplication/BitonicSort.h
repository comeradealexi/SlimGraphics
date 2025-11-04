#pragma once
#include <sgPlatformInclude.h>
#include "sgUploadHeap.h"
#include "ShaderSharedStructures.h"
#include "LinearConstantBuffer.h"

class BitonicSort
{
public:
	BitonicSort(sg::SharedPtr<sg::Device>& _device);

	void sort(sg::CommandList& command_list, sg::Buffer* buffer_input, sg::Buffer* buffer_output, sg::u32 array_size, SimpleLinearConstantBuffer& cbuffer);

private:
	sg::SharedPtr<sg::Pipeline> pipeline_tile_sort;
	sg::SharedPtr<sg::Pipeline> pipeline_merge;
};