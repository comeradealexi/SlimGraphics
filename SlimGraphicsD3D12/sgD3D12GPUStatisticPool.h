#pragma once
#include <sgTypes.h>
#include "sgD3D12Include.h"
#include <atomic>
namespace sg
{
	namespace D3D12
	{
		class GPUStatisticPool
		{
		public:
			GPUStatisticPool(ID3D12Device6* device, ID3D12CommandQueue* queue, u32 _max_count);
			using Index = u32;

			void resolve();
			void begin_frame();
			void end_frame(CommandList* command_list);
			
			Index allocate_new_query();

			void begin_query(Index index, CommandList* command_list);
			void end_query(Index index, CommandList* command_list);

			const D3D12_QUERY_DATA_PIPELINE_STATISTICS& collect_query(Index idx);


		private:
			bool recording = false;
			const u32 max_count;
			ComPtr<ID3D12QueryHeap> query_heap = nullptr;
			ComPtr<ID3D12Resource> readback_buffer = nullptr;
			std::atomic_uint32_t current_index;
			std::vector<D3D12_QUERY_DATA_PIPELINE_STATISTICS> stats_copy;
		};
	}
}
