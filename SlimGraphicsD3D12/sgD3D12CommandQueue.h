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
			void fence_signal(QueueFence* fence, u64 value);
			void fence_wait(QueueFence* fence, u64 value);

		private:
			ComPtr<ID3D12CommandQueue> queue;
		};
	}
}
