#include "pch.h"
#include "sgVulkanGPUTimestampPool.h"
#include "sgVulkanCommandList.h"

namespace sg
{
	namespace Vulkan
	{
		GPUTimestampPool::GPUTimestampPool(const vk::raii::Device& device, double period_ns, u32 valid_bits, u32 maximum_timestamps)
			: max_timestamps(maximum_timestamps), timestamp_period_ns(period_ns), timestamp_mask(valid_bits >= 64 ? ~0ull : ((1ull << valid_bits) - 1ull))
		{
			timestamps_copy.resize(static_cast<size_t>(max_timestamps) * 2);
			query_pool = vk::raii::QueryPool(device, vk::QueryPoolCreateInfo({}, vk::QueryType::eTimestamp, max_timestamps * 2));
			query_pool.reset(0, max_timestamps * 2); // Queries must be reset before first use (hostQueryReset)
		}

		void GPUTimestampPool::resolve()
		{
			seAssert(recording == false, "Recording should be false");
			const u32 query_count = current_index * 2;
			if (query_count == 0)
			{
				return;
			}
			// The CPU has already waited for the frame, so results are available. VK_NOT_READY is tolerated (values keep their previous contents).
			const VkResult result = query_pool.getDispatcher()->vkGetQueryPoolResults(static_cast<VkDevice>(query_pool.getDevice()), static_cast<VkQueryPool>(*query_pool), 0, query_count,
				query_count * sizeof(u64), timestamps_copy.data(), sizeof(u64), VK_QUERY_RESULT_64_BIT);
			seAssert(result == VK_SUCCESS || result == VK_NOT_READY, "vkGetQueryPoolResults failed (%i)", static_cast<int>(result));
			(void)result;
		}

		void GPUTimestampPool::begin_frame()
		{
			seAssert(recording == false, "Recording should be false");
			// Reset last frame's queries from the CPU (the previous frame has completed)
			if (current_index > 0)
			{
				query_pool.reset(0, current_index * 2);
			}
			recording = true;
			current_index = 0;
		}

		void GPUTimestampPool::end_frame(CommandList* command_list)
		{
			seAssert(recording, "Recording should be true");
			recording = false;
		}

		GPUTimestampPool::Index GPUTimestampPool::allocate_new_timestamp()
		{
			seAssert(current_index < max_timestamps, "GPUTimestampPool exhausted (max %u)", max_timestamps);
			if (current_index >= max_timestamps)
			{
				return max_timestamps - 1;
			}
			return current_index++;
		}

		void GPUTimestampPool::begin_timestamp(Index index, CommandList* command_list)
		{
			command_list->write_timestamp(*query_pool, (index * 2) + 0, vk::PipelineStageFlagBits2::eTopOfPipe);
		}

		void GPUTimestampPool::end_timestamp(Index index, CommandList* command_list)
		{
			command_list->write_timestamp(*query_pool, (index * 2) + 1, vk::PipelineStageFlagBits2::eBottomOfPipe);
		}

		double GPUTimestampPool::collect_timestamp_us(Index idx)
		{
			const u32 offset = idx * 2;
			const u64 begin = timestamps_copy[offset + 0] & timestamp_mask;
			const u64 end = timestamps_copy[offset + 1] & timestamp_mask;
			if (end < begin)
			{
				return 0.0;
			}
			return static_cast<double>(end - begin) * timestamp_period_ns / 1000.0;
		}
	}
}
