#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"

namespace sg
{
	namespace Vulkan
	{
		class CommandQueue
		{
		public:
			CommandQueue(Device& device, vk::raii::Queue& in_queue);
			vk::raii::Queue& get() { return queue; }

			void submit_command_list(CommandList* command_list);
			
			//Signals the fence on the GPU once all other commands complete
			void fence_signal(QueueFence& fence, u64 value);

			//Inserts a GPU wait to the queue
			void fence_wait_gpu(QueueFence& fence, u64 value);

			//Waits immediately on the CPU for the fence to be signaled
			void fence_wait_cpu(QueueFence& fence, u64 value);

		private:
			Device& sg_device;
			vk::raii::Queue queue = nullptr;
		};
	}
}
