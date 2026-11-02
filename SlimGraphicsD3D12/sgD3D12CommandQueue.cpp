#include "pch.h"
#include "sgD3D12CommandQueue.h"
#include "sgD3D12CommandList.h"
#include "sgD3D12QueueFence.h"

namespace sg
{
	namespace D3D12
	{
		CommandQueue::CommandQueue(ComPtr<ID3D12CommandQueue>& in_queue) : queue(in_queue)
		{
			cpu_fence = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		}

		void CommandQueue::submit_command_list(CommandList* command_list)
		{
			ID3D12CommandList* p_command_list = command_list->get().Get();
			queue->ExecuteCommandLists(1, &p_command_list);
		}

		void CommandQueue::fence_signal(QueueFence& fence, u64 value)
		{
			CHECKHR(queue->Signal(fence.d3d12_fence.Get(), value));
		}
		
		void CommandQueue::fence_wait_gpu(QueueFence& fence, u64 value)
		{
			CHECKHR(queue->Wait(fence.d3d12_fence.Get(), value));
		}
		void CommandQueue::fence_wait_cpu(QueueFence& fence, u64 value)
		{
			if (fence.d3d12_fence->GetCompletedValue() < value)
			{
				CHECKHR(fence.d3d12_fence->SetEventOnCompletion(value, cpu_fence));
				DWORD res = WaitForSingleObjectEx(cpu_fence, INFINITE, FALSE);
				seAssert(res == WAIT_OBJECT_0, "Irregular WaitForSingleObjectEx Return: %x", HRESULT_FROM_WIN32(GetLastError()));
			}
		}
	}
}