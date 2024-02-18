#pragma once
#include <sgTypes.h>
#include <atomic>
namespace sg
{
	namespace D3D12
	{
		class GPUTimestampPool
		{
		public:
			using Index = u32;

			void begin_frame();
			void end_frame();
			
			Index allocate_new_timestamp();

			void begin_timestamp(Index index, CommandList* command_list);
			void end_timestamp(Index index, CommandList* command_list);

			void collect_timestamps();

		private:
			double gpu_tick_delta = 0.0;
			ComPtr<ID3D12QueryHeap> query_heap = nullptr;
			ComPtr<ID3D12Resource> readback_buffer = nullptr;
			std::atomic_uint32_t current_index;
		};
	}
}
