#include "pch.h"
#include "sgD3D12CommandQueue.h"
#include "sgD3D12CommandList.h"

namespace sg
{
	namespace D3D12
	{
		CommandQueue::CommandQueue(ComPtr<ID3D12CommandQueue>& in_queue) : queue(in_queue)
		{

		}

		void CommandQueue::submit_command_list(CommandList* command_list)
		{
			ID3D12CommandList* p_command_list = command_list->get().Get();
			queue->ExecuteCommandLists(1, &p_command_list);
		}

		void CommandQueue::fence_signal(QueueFence* fence, u64 value)
		{
			queue->Signal(fence, value);
		}
		
		void CommandQueue::fence_wait(QueueFence* fence, u64 value)
		{
			queue->Wait(fence, value);
		}
	}
}