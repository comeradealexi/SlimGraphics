#include "BitonicSort.h"
#include "sgUtils.h"
#include <lodepng.h>
#include <imgui.h>
using namespace sg;


// README: This file serves as a proof of concept that it is functional.
// Needs updating if planned to be used properly.

BitonicSort::BitonicSort(sg::SharedPtr<sg::Device>& _device) : device(_device)
{
	BindingDesc pipeline_binding_desc = {};
	pipeline_binding_desc.uav_binding_count = 1;
	pipeline_binding_desc.srv_binding_count = 1;
	pipeline_binding_desc.cbv_binding_count = 2;
	PipelineDesc::Compute pipeline_desc = {};	

	pipeline_desc.compute_shader = create_compute_shader(*_device, "ShaderBin_Debug\\BitonicSort_LocalTileSort.PC_DXC");
	pipeline_tile_sort = _device->create_pipeline(pipeline_desc, pipeline_binding_desc);

	pipeline_desc.compute_shader = create_compute_shader(*_device, "ShaderBin_Debug\\BitonicSort_Merge.PC_DXC");
	pipeline_merge = _device->create_pipeline(pipeline_desc, pipeline_binding_desc);

	buffer_upload = create_buffer(*device, 1024 * 1024, BufferType::Upload, false);
	buffer_zerod = create_buffer(*device, 1024 * 1024, BufferType::Upload, false);
	buffer_readback = create_readback_buffer(*device, 1024 * 1024);
	buffer_input = create_buffer(*device, 1024 * 1024, BufferType::GeneralDataBuffer, true);
	buffer_output = create_buffer(*device, 1024 * 1024, BufferType::GeneralDataBuffer, true);
}

void BitonicSort::sort(sg::CommandList& command_list, /*sg::Buffer* buffer_input, sg::Buffer* buffer_output, sg::u32 array_size, */SimpleLinearConstantBuffer& cbuffer)
{

	if (run_once == false)
	{
		if (readback_once)
		{
			sorted_list.resize(8192);
			void* m = buffer_readback->map_memory();
			memcpy(sorted_list.data(), m, sizeof(KeyValue) * 8192);
			buffer_readback->unmap_memory();

			for (size_t i = 1; i < 8192; i++)
			{
				seAssert(sorted_list[i].key >= sorted_list[i - 1].key, "Not Sorted!");
			}

			readback_once = false;
		}
		return; 
	}
	run_once = false;

	stats = {};


	struct SortParams
	{
		uint32_t g_NumElements;
		uint32_t g_TileSize;
		uint32_t g_Ascending;
		uint32_t pad0;
	} sort_params = {};

	struct GlobalMergeParams
	{
		uint32_t g_k;
		uint32_t g_j;
		uint32_t g_dummy0;
		uint32_t g_dummy1;
	} global_merge_params = {};

	const uint32_t TILE_SIZE = 32;
	const uint32_t sort_count = 8192;
	KeyValue* mapped_memory = (KeyValue*)buffer_upload->map_memory();
	srand(32);
	for (uint32_t i = 0; i < sort_count; i++)
	{
		mapped_memory[i].key = rand() % sort_count;
		mapped_memory[i].value = i;
	}
	buffer_upload->unmap_memory();

	{
		void* zerod = buffer_zerod->map_memory();
		memset(zerod, 0xDEADBEEF, 1024 * 1024);
		buffer_zerod->unmap_memory();
	}

	command_list.copy_buffer_to_buffer(buffer_input.get(), buffer_upload.get());

	Binding b;
	b.srv_binding_count = 1;
	b.uav_binding_count = 1;
	b.cbv_binding_count = 2;


	bool buffer_ping_pong = false;

	{
		command_list.set_pipeline(pipeline_tile_sort.get());
		sg::ShaderResourceView input_srv = device->create_shader_resource_view(buffer_input, sizeof(KeyValue), sort_count);
		b.set_srv(input_srv, 0);

		sg::UnorderedAccessView output_uav = device->create_unordered_access_view(buffer_output, sizeof(KeyValue), sort_count);
		b.set_uav(output_uav, 0);

		sort_params.g_NumElements = sort_count;
		sort_params.g_TileSize = TILE_SIZE;
		sort_params.g_Ascending = 1;
		sg::ConstantBufferView cbv0 = cbuffer.AllocateAndWrite(sort_params);
		sg::ConstantBufferView cbv1 = cbuffer.AllocateAndWrite(global_merge_params);
		b.set_cbv(cbv0, 0);
		b.set_cbv(cbv1, 1);

		command_list.bind(b, PipelineType::Compute);
		command_list.dispatch(sort_count / TILE_SIZE, 1, 1);

		// Merge
		{
			buffer_ping_pong = true;
			output_uav = device->create_unordered_access_view(buffer_output, sizeof(KeyValue), sort_count);

			command_list.set_pipeline(pipeline_merge.get());
			// 2) Phase B: Hierarchical global merges
			// Start k from 2 * TILE_SIZE (we already did k <= TILE_SIZE inside local tiles)
			for (uint32_t k = 2u * TILE_SIZE; k <= sort_count; k <<= 1u)
			{
				for (uint32_t j = k >> 1u; j >= 1; j >>= 1u)
				{
					global_merge_params = { k, j, /*g_Ascending*/ 1, /*padding*/0 };
					cbv1 = cbuffer.AllocateAndWrite(global_merge_params);
					b.set_cbv(cbv1, 1);
					b.set_srv(input_srv, 0);
					b.set_uav(output_uav, 0);
					
					command_list.bind(b, PipelineType::Compute);

					// Dispatch one thread per element
					uint32_t groups = (sort_count + TILE_SIZE - 1u) / TILE_SIZE;
					command_list.dispatch(groups / 2, 1, 1);
					stats.dispatch_count++;
				}
			}
		}
	}

	command_list.copy_buffer_to_buffer(buffer_readback.get(), buffer_output.get());
}