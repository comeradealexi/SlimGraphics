#include "pch.h"
#include "sgD3D12GPUStatisticPool.h"
#include "sgD3D12CommandList.h"

namespace sg
{
	namespace D3D12
	{
		GPUStatisticPool::GPUStatisticPool(ID3D12Device6* device, ID3D12CommandQueue* queue, u32 _max_count) : max_count(_max_count)
		{
			stats_copy.resize(max_count);
			
			D3D12_HEAP_PROPERTIES HeapProps;
			HeapProps.Type = D3D12_HEAP_TYPE_READBACK;
			HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			HeapProps.CreationNodeMask = 1;
			HeapProps.VisibleNodeMask = 1;

			D3D12_RESOURCE_DESC BufferDesc;
			BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			BufferDesc.Alignment = 0;
			BufferDesc.Width = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) * max_count;
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
			readback_buffer->SetName(L"GPU Statistic Buffer");

			D3D12_QUERY_HEAP_DESC QueryHeapDesc;
			QueryHeapDesc.Count = max_count;
			QueryHeapDesc.NodeMask = 1;
			QueryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
			CHECKHR(device->CreateQueryHeap(&QueryHeapDesc, IID_PPV_ARGS(query_heap.GetAddressOf())));
			query_heap->SetName(L"GPU Statistic QueryHeap");
		}

		void GPUStatisticPool::resolve()
		{
			seAssert(recording == false, "Recording should be false");
			D3D12_RANGE Range;
			Range.Begin = 0;
			Range.End = current_index * sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
			void* ptr;
			CHECKHR(readback_buffer->Map(0, &Range, reinterpret_cast<void**>(&ptr)));
			{
				memcpy(stats_copy.data(), ptr, Range.End);
			}
			D3D12_RANGE EmptyRange = {};
			readback_buffer->Unmap(0, &EmptyRange);
		}

		void GPUStatisticPool::begin_frame()
		{
			seAssert(recording == false, "Recording should be false");
			recording = true;
			current_index = 0;
		}

		void GPUStatisticPool::end_frame(CommandList* command_list)
		{
			seAssert(recording, "Recording should be false");
			u32 query_count = current_index;
			command_list->get()->ResolveQueryData(query_heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, query_count, readback_buffer.Get(), 0);
			recording = false;
		}

		GPUStatisticPool::Index GPUStatisticPool::allocate_new_query()
		{
			return current_index++;
		}

		void GPUStatisticPool::begin_query(Index index, CommandList* command_list)
		{
			command_list->get()->BeginQuery(query_heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, index);
		}

		void GPUStatisticPool::end_query(Index index, CommandList* command_list)
		{
			command_list->get()->EndQuery(query_heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, index);
		}

		const D3D12_QUERY_DATA_PIPELINE_STATISTICS& GPUStatisticPool::collect_query(Index idx)
		{
			return stats_copy[idx];
		}

	}
}
