#pragma once
#include <sgTypes.h>
#include "sgD3D12Include.h"
#include <atomic>
namespace sg
{
	namespace D3D12
	{
		class GPUTimestampPool
		{
		public:
			GPUTimestampPool(ID3D12Device* device, ID3D12CommandQueue* queue, u32 maximum_timestamps);
			using Index = u32;

			void resolve();
			void begin_frame();
			void end_frame(CommandList* command_list);
			
			Index allocate_new_timestamp();

			void begin_timestamp(Index index, CommandList* command_list);
			void end_timestamp(Index index, CommandList* command_list);

			double collect_timestamp_us(Index idx);

		private:
			bool recording = false;
			const u32 max_timestamps;
			double gpu_tick_delta = 0.0;
			ComPtr<ID3D12QueryHeap> query_heap = nullptr;
			ComPtr<ID3D12Resource> readback_buffer = nullptr;
			std::atomic_uint32_t current_index;
			std::vector<u64> timestamps_copy;
		};
	}
}
