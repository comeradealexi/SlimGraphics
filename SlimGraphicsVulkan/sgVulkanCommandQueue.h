#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"
#include <deque>

namespace sg
{
	namespace Vulkan
	{
		// Submits to the device's single graphics + compute + present queue.
		class CommandQueue
		{
		public:
			explicit CommandQueue(Device& device);
			CommandQueue(const CommandQueue&) = delete;
			CommandQueue& operator=(const CommandQueue&) = delete;

			void submit_command_list(CommandList* command_list);

			//Signals the fence on the GPU once all other commands complete
			void fence_signal(QueueFence& fence, u64 value);

			//Inserts a GPU wait to the queue
			void fence_wait_gpu(QueueFence& fence, u64 value);

			//Waits immediately on the CPU for the fence to be signaled
			void fence_wait_cpu(QueueFence& fence, u64 value);

			// Submits a batch with no command buffers that waits on / signals binary semaphores (swap chain synchronisation).
			// Semaphore waits/signals at ALL_COMMANDS order against all earlier/later work on the queue.
			void submit_semaphores(vk::Semaphore wait_semaphore, vk::Semaphore signal_semaphore);

		private:
			// Records pending first-use image layout transitions (see Device::add_pending_initial_transition)
			// into a command buffer that runs before the next submitted command list.
			vk::CommandBuffer record_pending_initialisation();

			Device& device;

			// Internal timeline used to know when initialisation command buffers can be reused
			vk::raii::Semaphore internal_timeline = nullptr;
			u64 internal_timeline_value = 0;
			vk::raii::CommandPool initialisation_pool = nullptr;
			struct InitialisationCommandBuffer
			{
				vk::raii::CommandBuffer command_buffer = nullptr;
				u64 completion_value = 0;
			};
			std::deque<InitialisationCommandBuffer> initialisation_command_buffers;
			std::vector<vk::ImageMemoryBarrier2> initialisation_barriers; // Reused to avoid per submission allocations
		};
	}
}
