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
			return (u32)~0;
		}

		void DescriptorHeap::free(u32 heap_index)
		{
			std::lock_guard lg(mutex);
			seAssert(free_list[heap_index] == true, "Freeing descriptor to bad index");
			free_list[heap_index] = false;
		}
	}
}