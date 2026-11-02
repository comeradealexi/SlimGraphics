#include "pch.h"
#include "sgVulkanCommandQueue.h"
#include "sgVulkanCommandList.h"

namespace sg
{
	namespace Vulkan
	{
		#if SG_PLATFORM_D3D12
		CommandQueue::CommandQueue(ComPtr<ID3D12CommandQueue>& in_queue) : queue(in_queue)
		{
			cpu_fence = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		}
		#endif

		void CommandQueue::submit_command_list(CommandList* command_list)
		{
			seAssert(false, "VULKAN TODO " __FILE__ __FUNCTION__);
			#if SG_PLATFORM_D3D12
			ID3D12CommandList* p_command_list = command_list->get().Get();
			queue->ExecuteCommandLists(1, &p_command_list);
			#endif
		}

		void CommandQueue::fence_signal(QueueFence* fence, u64 value)
		{
			seAssert(false, "VULKAN TODO " __FILE__ __FUNCTION__);
			#if SG_PLATFORM_D3D12
			CHECKHR(queue->Signal(fence, value));
			#endif
		}
		
		void CommandQueue::fence_wait_gpu(QueueFence* fence, u64 value)
		{
			seAssert(false, "VULKAN TODO " __FILE__ __FUNCTION__);
			#if SG_PLATFORM_D3D12
			CHECKHR(queue->Wait(fence, value));
			#endif
		}
		void CommandQueue::fence_wait_cpu(QueueFence* fence, u64 value)
		{
			seAssert(false, "VULKAN TODO " __FILE__ __FUNCTION__);
			#if SG_PLATFORM_D3D12
			if (fence->GetCompletedValue() < value)
			{
				CHECKHR(fence->SetEventOnCompletion(value, cpu_fence));
				DWORD res = WaitForSingleObjectEx(cpu_fence, INFINITE, FALSE);
				seAssert(res == WAIT_OBJECT_0, "Irregular WaitForSingleObjectEx Return: %x", HRESULT_FROM_WIN32(GetLastError()));
			}
			#endif
		}
	}
}