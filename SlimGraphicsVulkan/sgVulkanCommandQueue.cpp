#include "pch.h"
#include "sgVulkanCommandQueue.h"
#include "sgVulkanCommandList.h"
#include "sgVulkanDevice.h"
#include "sgVulkanQueueFence.h"

namespace sg
{
	namespace Vulkan
	{
		CommandQueue::CommandQueue(Device& device_) : device(device_)
		{
			const vk::raii::Device& vk_device = device.get_device();
			const vk::SemaphoreTypeCreateInfo timeline_info(vk::SemaphoreType::eTimeline, 0);
			internal_timeline = CHECKVK(vk_device.createSemaphore(vk::SemaphoreCreateInfo({}, &timeline_info)));
			initialisation_pool = CHECKVK(vk_device.createCommandPool(vk::CommandPoolCreateInfo(
				vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer, device.get_queue_family())));
		}

		vk::CommandBuffer CommandQueue::record_pending_initialisation()
		{
			device.take_pending_initial_transitions(initialisation_barriers);
			if (initialisation_barriers.empty())
			{
				return VK_NULL_HANDLE;
			}

			// Reuse a command buffer whose previous submission has completed
			const u64 completed = CHECKVK(internal_timeline.getCounterValue());
			InitialisationCommandBuffer* entry = nullptr;
			if (!initialisation_command_buffers.empty() && initialisation_command_buffers.front().completion_value <= completed)
			{
				initialisation_command_buffers.push_back(std::move(initialisation_command_buffers.front()));
				initialisation_command_buffers.pop_front();
				entry = &initialisation_command_buffers.back();
				CHECKVK(entry->command_buffer.reset());
			}
			else
			{
				std::vector<vk::raii::CommandBuffer> buffers = CHECKVK(device.get_device().allocateCommandBuffers(
					vk::CommandBufferAllocateInfo(*initialisation_pool, vk::CommandBufferLevel::ePrimary, 1)));
				initialisation_command_buffers.push_back({ std::move(buffers[0]), 0 });
				entry = &initialisation_command_buffers.back();
			}

			entry->completion_value = ++internal_timeline_value;
			CHECKVK(entry->command_buffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)));
			entry->command_buffer.pipelineBarrier2(vk::DependencyInfo({}, {}, {}, initialisation_barriers));
			CHECKVK(entry->command_buffer.end());
			initialisation_barriers.clear();
			return *entry->command_buffer;
		}

		void CommandQueue::submit_command_list(CommandList* command_list)
		{
			std::array<vk::CommandBufferSubmitInfo, 2> command_buffers;
			u32 command_buffer_count = 0;
			vk::SemaphoreSubmitInfo signal_info;
			u32 signal_count = 0;

			if (const vk::CommandBuffer initialisation = record_pending_initialisation())
			{
				command_buffers[command_buffer_count++] = vk::CommandBufferSubmitInfo(initialisation);
				signal_info = vk::SemaphoreSubmitInfo(*internal_timeline, internal_timeline_value, vk::PipelineStageFlagBits2::eAllCommands);
				signal_count = 1;
			}
			command_buffers[command_buffer_count++] = vk::CommandBufferSubmitInfo(command_list->get_command_buffer());

			const vk::SubmitInfo2 submit_info({}, 0, nullptr, command_buffer_count, command_buffers.data(), signal_count, &signal_info);
			CHECKVK(device.get_queue().submit2(submit_info));
		}

		void CommandQueue::fence_signal(QueueFence& fence, u64 value)
		{
			const vk::SemaphoreSubmitInfo signal_info(*fence.timeline_semaphore, value, vk::PipelineStageFlagBits2::eAllCommands);
			CHECKVK(device.get_queue().submit2(vk::SubmitInfo2({}, {}, {}, signal_info)));
		}

		void CommandQueue::fence_wait_gpu(QueueFence& fence, u64 value)
		{
			const vk::SemaphoreSubmitInfo wait_info(*fence.timeline_semaphore, value, vk::PipelineStageFlagBits2::eAllCommands);
			CHECKVK(device.get_queue().submit2(vk::SubmitInfo2({}, wait_info, {}, {})));
		}

		void CommandQueue::fence_wait_cpu(QueueFence& fence, u64 value)
		{
			const vk::Semaphore semaphore = *fence.timeline_semaphore;
			CHECKVK(device.get_device().waitSemaphores(vk::SemaphoreWaitInfo({}, semaphore, value), UINT64_MAX));
		}

		void CommandQueue::submit_semaphores(vk::Semaphore wait_semaphore, vk::Semaphore signal_semaphore)
		{
			const vk::SemaphoreSubmitInfo wait_info(wait_semaphore, 0, vk::PipelineStageFlagBits2::eAllCommands);
			const vk::SemaphoreSubmitInfo signal_info(signal_semaphore, 0, vk::PipelineStageFlagBits2::eAllCommands);
			const vk::SubmitInfo2 submit_info({}, wait_semaphore ? 1u : 0u, &wait_info, 0, nullptr, signal_semaphore ? 1u : 0u, &signal_info);
			CHECKVK(device.get_queue().submit2(submit_info));
		}
	}
}
