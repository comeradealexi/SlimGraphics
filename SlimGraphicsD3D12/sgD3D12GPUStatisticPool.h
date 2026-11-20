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

			sg::PipelineStatistics collect_query(Index idx);


		private:
			bool recording = false;
			const u32 max_count;
			ComPtr<ID3D12QueryHeap> query_heap = nullptr;
			ComPtr<ID3D12Resource> readback_buffer = nullptr;
			std::atomic_uint32_t current_index = 0;
			// PIPELINE_STATISTICS1 adds the amplification / mesh shader counters (when the device supports it). Its data starts
			// with the PIPELINE_STATISTICS fields, so results are kept in that layout either way.
			bool mesh_statistics = false;
			D3D12_QUERY_TYPE query_type = D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
			UINT query_size = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
			std::vector<D3D12_QUERY_DATA_PIPELINE_STATISTICS1> stats_copy;
		};
	}
}
