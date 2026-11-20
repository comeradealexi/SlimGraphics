#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"

namespace sg
{
	namespace Vulkan
	{
		class GPUTimestampPool
		{
		public:
			GPUTimestampPool(const vk::raii::Device& device, double timestamp_period_ns, u32 timestamp_valid_bits, u32 maximum_timestamps);
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
			const double timestamp_period_ns;
			const u64 timestamp_mask;
			u32 current_index = 0;
			vk::raii::QueryPool query_pool = nullptr;
			std::vector<u64> timestamps_copy;
		};
	}
}
