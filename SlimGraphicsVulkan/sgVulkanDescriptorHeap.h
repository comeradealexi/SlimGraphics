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
			static constexpr u32 INVALID_HEAP_INDEX = (u32)~0;
			DescriptorHeap(ComPtr<ID3D12DescriptorHeap> d3d12_heap, u32 count, u32 in_heap_increment_size);

			u32 allocate();
			void free(u32 heap_index);

			D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_handle_heap_start(void) const { return heap->GetCPUDescriptorHandleForHeapStart(); };
			D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_handle_heap_start(void) const { return heap->GetGPUDescriptorHandleForHeapStart(); };

			CD3DX12_CPU_DESCRIPTOR_HANDLE get_cpu_handle_at_offset(u32 index_offset);
			CD3DX12_GPU_DESCRIPTOR_HANDLE get_gpu_handle_at_offset(u32 index_offset);

			inline u32 get_increment_size() const { return heap_increment_size; }
			ComPtr<ID3D12DescriptorHeap> get_heap() { return heap; }
		private:
			std::mutex mutex;
			u32 current_index = 0;
			const u32 heap_increment_size;
			ComPtr<ID3D12DescriptorHeap> heap;
			std::vector<bool> free_list;
		};
	}
}