#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"
#include <atomic>
namespace sg
{
	namespace Vulkan
	{
		class GPUStatisticPool
		{
		public:
#if SG_PLATFORM_D3D12
			GPUStatisticPool(ID3D12Device6* device, ID3D12CommandQueue* queue, u32 _max_count);
#endif
			using Index = u32;

			void resolve();
			void begin_frame();
			void end_frame(CommandList* command_list);
			
			Index allocate_new_query();

			void begin_query(Index index, CommandList* command_list);
			void end_query(Index index, CommandList* command_list);

#if SG_PLATFORM_D3D12
			const D3D12_QUERY_DATA_PIPELINE_STATISTICS& collect_query(Index idx);
#endif


		private:
			bool recording = false;
			const u32 max_count;

#if SG_PLATFORM_D3D12
			ComPtr<ID3D12QueryHeap> query_heap = nullptr;
			ComPtr<ID3D12Resource> readback_buffer = nullptr;
			std::atomic_uint32_t current_index;
			std::vector<D3D12_QUERY_DATA_PIPELINE_STATISTICS> stats_copy;
#endif
		};
	}
}
