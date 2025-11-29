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

		//buffer_ping_pong = true;
		//goto labelalex;
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

					// read from 'input', write to 'output'
					//input_srv = device->create_shader_resource_view(buffer_ping_pong ? buffer_output : buffer_input, sizeof(KeyValue), sort_count);
					//output_uav = device->create_unordered_access_view(buffer_ping_pong ? buffer_input : buffer_output, sizeof(KeyValue), sort_count);
					
					//command_list.copy_buffer_to_buffer(buffer_ping_pong ? buffer_input.get() : buffer_output.get(), buffer_zerod.get());


					b.set_srv(input_srv, 0);
					b.set_uav(output_uav, 0);
					
					command_list.bind(b, PipelineType::Compute);

					// Dispatch one thread per element
					uint32_t groups = (sort_count + TILE_SIZE - 1u) / TILE_SIZE;
					command_list.dispatch(groups / 2, 1, 1);
					stats.dispatch_count++;

					// Insert UAV barrier / memory barrier here (DX12: UAVBarrier; DX11: Dispatch then UnorderedAccessView barrier / Flush)
					//buffer_ping_pong = !buffer_ping_pong;
					//if (j == 8)
					//goto labelalex;

				}
				//goto labelalex;
			}
		}
	}
	labelalex:
	command_list.copy_buffer_to_buffer(buffer_readback.get(), buffer_output.get());
}

// // ---------- Constants ----------
// cbuffer SortParams : register(b0)
// {
// 	uint g_NumElements;   // array length (power-of-two, padded if necessary)
// 	uint g_TileSize;      // should equal TILE_SIZE at runtime
// 	uint g_Ascending;     // 1 = ascending, 0 = descending (global direction)
// 	uint pad0;
// 	// For GlobalMergeCS, we will set g_k and g_j via separate params (see below)
// };
// 
// // Local tile params (can be same buffer, but we'll read only necessary fields)
// cbuffer GlobalMergeParams : register(b1)
// {
// 	uint g_k;   // subsequence size for this pass (2, 4, 8, ... up to N)
// 	uint g_j;   // half-subsequence (k >> 1)
// 	uint g_dummy0;
// 	uint g_dummy1;
// }

//	// PSEUDO-CODE (DirectX 11/12 style):
//	// Input: gpuBufferA (initial data), gpuBufferB (temp), NUM_ELEMENTS, TILE_SIZE
//	// Both buffers must hold NUM_ELEMENTS KeyValue elements (pad to pow2).
//	
//	ID3D11UnorderedAccessView* uavA, * uavB;
//	ID3D11ShaderResourceView* srvA, * srvB;
//	
//	Buffer* input = gpuBufferA;   // start with input
//	Buffer* output = gpuBufferB;  // temp
//	
//	// 1) Phase A: Local tile sort
//	LocalParams localParams = { NUM_ELEMENTS, TILE_SIZE, 1 /*ascending*/, 0 };
//	UpdateConstantBuffer(sortParamBuffer, &localParams);
//	
//	BindComputeShader(LocalTileSortCS); // compiled with THREADS_PER_GROUP == TILE_SIZE
//	SetSRV(0, inputSRV);
//	SetUAV(0, outputUAV);
//	
//	// Dispatch groups = NUM_ELEMENTS / TILE_SIZE
//	Dispatch(NUM_ELEMENTS / TILE_SIZE, 1, 1);
//	// Insert UAV barrier / memory barrier here (API specific)
//	std::swap(input, output);
//	
//	// 2) Phase B: Hierarchical global merges
//	// Start k from 2 * TILE_SIZE (we already did k <= TILE_SIZE inside local tiles)
//	for (uint32_t k = 2u * TILE_SIZE; k <= NUM_ELEMENTS; k <<= 1u)
//	{
//		for (uint32_t j = k >> 1u; j >= TILE_SIZE; j >>= 1u)
//		{
//			GlobalParams g = { k, j, /*g_Ascending*/ 1, /*padding*/0 };
//			UpdateConstantBuffer(globalParamBuffer, &g);
//	
//			BindComputeShader(GlobalMergeCS);
//			// read from 'input', write to 'output'
//			SetSRV(0, inputSRV);
//			SetUAV(0, outputUAV);
//	
//			// Dispatch one thread per element
//			uint32_t groups = (NUM_ELEMENTS + THREADS_PER_GROUP - 1u) / THREADS_PER_GROUP;
//			Dispatch(groups, 1, 1);
//	
//			// Insert UAV barrier / memory barrier here (DX12: UAVBarrier; DX11: Dispatch then UnorderedAccessView barrier / Flush)
//			std::swap(input, output);
//		}
//	}
//	
//	// After loops, 'input' contains the sorted array (if last swap left it there).
//	// If final result is in the 'output' buffer, copy it back or swap handles accordingly.
