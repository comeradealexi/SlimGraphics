#include "pch.h"
#include "sgD3D12GPUTimestampPool.h"
#include "sgD3D12CommandList.h"

namespace sg
{
	namespace D3D12
	{
		GPUTimestampPool::GPUTimestampPool(ID3D12Device* device, ID3D12CommandQueue* queue, u32 maximum_timestamps) : max_timestamps(maximum_timestamps)
		{
			timestamps_copy.resize(max_timestamps);

			//GPUFreq
			{
				uint64_t GpuFrequency;
				CHECKHR(queue->GetTimestampFrequency(&GpuFrequency));
				gpu_tick_delta = 1.0 / static_cast<double>(GpuFrequency);
			}

			D3D12_HEAP_PROPERTIES HeapProps;
			HeapProps.Type = D3D12_HEAP_TYPE_READBACK;
			HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			HeapProps.CreationNodeMask = 1;
			HeapProps.VisibleNodeMask = 1;

			D3D12_RESOURCE_DESC BufferDesc;
			BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			BufferDesc.Alignment = 0;
			BufferDesc.Width = sizeof(uint64_t) * max_timestamps * 2;
			BufferDesc.Height = 1;
			BufferDesc.DepthOrArraySize = 1;
			BufferDesc.MipLevels = 1;
			BufferDesc.Format = DXGI_FORMAT_UNKNOWN;
			BufferDesc.SampleDesc.Count = 1;
			BufferDesc.SampleDesc.Quality = 0;
			BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			BufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			CHECKHR(device->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &BufferDesc,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback_buffer.GetAddressOf())));
			readback_buffer->SetName(L"GPU Timestamp Buffer");

			D3D12_QUERY_HEAP_DESC QueryHeapDesc;
			QueryHeapDesc.Count = max_timestamps * 2;
			QueryHeapDesc.NodeMask = 1;
			QueryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
			CHECKHR(device->CreateQueryHeap(&QueryHeapDesc, IID_PPV_ARGS(query_heap.GetAddressOf())));
			query_heap->SetName(L"GPU Timestamp QueryHeap");
		}

		void GPUTimestampPool::resolve()
		{
			seAssert(recording == false, "Recording should be false");
			D3D12_RANGE Range;
			Range.Begin = 0;
			Range.End = (current_index * 2) * sizeof(uint64_t);
			void* ptr;
			CHECKHR(readback_buffer->Map(0, &Range, reinterpret_cast<void**>(&ptr)));
			{
				memcpy(timestamps_copy.data(), ptr, Range.End);
			}
			D3D12_RANGE EmptyRange = {};
			readback_buffer->Unmap(0, &EmptyRange);
		}

		void GPUTimestampPool::begin_frame()
		{
			seAssert(recording == false, "Recording should be false");
			recording = true;
			current_index = 0;
		}

		void GPUTimestampPool::end_frame(CommandList* command_list)
		{
			seAssert(recording, "Recording should be false");
			u32 query_count = current_index * 2;
			command_list->get()->ResolveQueryData(query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, query_count, readback_buffer.Get(), 0);
			recording = false;
		}

		GPUTimestampPool::Index GPUTimestampPool::allocate_new_timestamp()
		{
			return current_index++;
		}

		void GPUTimestampPool::begin_timestamp(Index index, CommandList* command_list)
		{
			command_list->get()->EndQuery(query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, (index * 2) + 0);
		}

		void GPUTimestampPool::end_timestamp(Index index, CommandList* command_list)
		{
			command_list->get()->EndQuery(query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, (index * 2) + 1);
		}

		double GPUTimestampPool::collect_timestamp_us(Index idx)
		{
			u32 offset = idx * 2;
			return static_cast<float>(1000000.0 * (gpu_tick_delta * (timestamps_copy[offset + 1] - timestamps_copy[offset + 0])));
		}
	}
}
