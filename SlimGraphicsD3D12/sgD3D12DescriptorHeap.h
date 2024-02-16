#pragma once
#include <sgTypes.h>
#include "sgD3D12Include.h"
#include <mutex>

namespace sg
{
	namespace D3D12
	{
		class DescriptorHeap
		{
		public:
			DescriptorHeap(ComPtr<ID3D12DescriptorHeap> d3d12_heap, u32 count, u32 in_heap_increment_size);

			u32 allocate();
			void free(u32 heap_index);

			D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_handle_heap_start(void) { return heap->GetCPUDescriptorHandleForHeapStart(); };
			D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_handle_heap_start(void) { return heap->GetGPUDescriptorHandleForHeapStart(); };

			inline u32 get_increment_size() { return heap_increment_size; }

		private:
			std::mutex mutex;
			u32 current_index = 0;
			const u32 heap_increment_size;
			ComPtr<ID3D12DescriptorHeap> heap;
			std::vector<bool> free_list;
		};
	}
}