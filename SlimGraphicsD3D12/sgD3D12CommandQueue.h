#pragma once
#include <sgTypes.h>
#include "sgD3D12Include.h"

namespace sg
{
	namespace D3D12
	{
		class CommandQueue
		{
		public:
			CommandQueue(ComPtr<ID3D12CommandQueue>& in_queue);
			ComPtr<ID3D12CommandQueue> get() { return queue; }

			void submit_command_list(CommandList* command_list);
			
			//Signals the fence on the GPU once all other commands complete
			void fence_signal(QueueFence* fence, u64 value);

			//Inserts a GPU wait to the queue
			void fence_wait_gpu(QueueFence* fence, u64 value);

			//Waits immediately on the CPU for the fence to be signalled
			void fence_wait_cpu(QueueFence* fence, u64 value);

		private:
			ComPtr<ID3D12CommandQueue> queue;
			HANDLE cpu_fence;
		};
	}
}
