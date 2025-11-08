#pragma once
#include <sgPlatformInclude.h>
#include "sgUploadHeap.h"
#include <DirectXMath.h>
#include "ShaderSharedStructures.h"
#include "LinearConstantBuffer.h"

class BitonicSort
{
public:
	BitonicSort(sg::SharedPtr<sg::Device>& _device);

	void sort(sg::CommandList& command_list, /*sg::Buffer* buffer_input, sg::Buffer* buffer_output, sg::u32 array_size, */SimpleLinearConstantBuffer& cbuffer);

private:
	struct KeyValue
	{
		uint32_t key;
		uint32_t value;
	};
	static_assert(sizeof(KeyValue) == (sizeof(uint32_t) * 2));
	bool run_once = true;
	bool readback_once = true;
	std::vector<KeyValue> sorted_list;
	sg::SharedPtr<sg::Pipeline> pipeline_tile_sort;
	sg::SharedPtr<sg::Pipeline> pipeline_merge;
	sg::SharedPtr<sg::Device> device;

	sg::SharedPtr<sg::Buffer> buffer_upload;
	sg::SharedPtr<sg::Buffer> buffer_zerod;
	sg::SharedPtr<sg::Buffer> buffer_input;
	sg::SharedPtr<sg::Buffer> buffer_output;
	sg::SharedPtr<sg::Buffer> buffer_readback;

};