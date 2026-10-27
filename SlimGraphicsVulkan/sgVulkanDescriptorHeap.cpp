#include "pch.h"
#include "sgD3D12DescriptorHeap.h"

namespace sg
{
	namespace D3D12
	{
		DescriptorHeap::DescriptorHeap(ComPtr<ID3D12DescriptorHeap> d3d12_heap, u32 count, u32 in_heap_increment_size) : heap_increment_size(in_heap_increment_size), heap(d3d12_heap)
		{
			free_list.resize(count);
		}

		u32 DescriptorHeap::allocate()
		{
			std::lock_guard lg(mutex);
			u32 len = (u32)free_list.size();
			for (u32 i = 0; i < len; i++, current_index++)
			{
				const u32 idx = current_index % len;
				if (free_list[idx] == false)
				{
					free_list[idx] = true;
					return idx;
				}
			}
			seAssert(false, "Ran out of descriptors");
			return DescriptorHeap::INVALID_HEAP_INDEX;
		}

		void DescriptorHeap::free(u32 heap_index)
		{
			if (heap_index != DescriptorHeap::INVALID_HEAP_INDEX)
			{
				std::lock_guard lg(mutex);
				seAssert(free_list[heap_index] == true, "Freeing descriptor to bad index");
				free_list[heap_index] = false;
			}
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::get_cpu_handle_at_offset(u32 index_offset)
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(heap->GetCPUDescriptorHandleForHeapStart(), index_offset, heap_increment_size);
		}

		CD3DX12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::get_gpu_handle_at_offset(u32 index_offset)
		{
			return CD3DX12_GPU_DESCRIPTOR_HANDLE(heap->GetGPUDescriptorHandleForHeapStart(), index_offset, heap_increment_size);
		}

	}
}