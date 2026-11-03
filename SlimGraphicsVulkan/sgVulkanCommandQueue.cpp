#include "pch.h"
#include "sgVulkanCommandQueue.h"
#include "sgVulkanCommandList.h"
#include "sgVulkanDevice.h"
#include "sgVulkanQueueFence.h"

namespace sg
{
	namespace Vulkan
	{
		/*
		
		// 1. Create the fence
// It is highly recommended to start it in the "Signaled" state so your
// very first frame/wait loop doesn't block infinitely.
vk::FenceCreateInfo fenceInfo(vk::FenceCreateFlagBits::eSignaled);
vk::raii::Fence fence(device, fenceInfo);

// 2. Prepare your queue submission
// Tie your command buffer to the fence during submission
vk::SubmitInfo submitInfo;
submitInfo.setCommandBufferCount(1);
submitInfo.setCommandBuffers(*commandBuffer); // Your vk::raii::CommandBuffer

// 3. Trigger the fence on the Queue
// The GPU will signal this fence when all work in this submit completes
queue.submit(submitInfo, *fence);

// 4. Wait on the CPU
// Pass the raw vk::Fence handle, a boolean to wait for all fences, and a timeout in nanoseconds
uint64_t timeoutNs = std::numeric_limits<uint64_t>::max(); // Wait indefinitely
vk::Result waitResult = device.waitForFences(*fence, VK_TRUE, timeoutNs);

// 5. Reset the fence before re-using it
// A fence must be reset back to the "Unsignaled" state before it can be submitted again
device.resetFences(*fence);
		
		*/
		CommandQueue::CommandQueue(Device& device, vk::raii::Queue& in_queue) : sg_device(device), queue(in_queue)
		{

		}

		void CommandQueue::submit_command_list(CommandList* command_list)
		{
			seAssert(false, "VULKAN TODO " __FILE__ __FUNCTION__);
			#if SG_PLATFORM_D3D12
			ID3D12CommandList* p_command_list = command_list->get().Get();
			queue->ExecuteCommandLists(1, &p_command_list);
			#endif
		}

		void CommandQueue::fence_signal(QueueFence& fence, u64 value)
		{
			seAssert(false, "VULKAN TODO " __FILE__ __FUNCTION__);
			#if SG_PLATFORM_D3D12
			CHECKHR(queue->Signal(fence, value));
			#endif
		}
		
		void CommandQueue::fence_wait_gpu(QueueFence& fence, u64 value)
		{
			seAssert(false, "VULKAN TODO " __FILE__ __FUNCTION__);
			#if SG_PLATFORM_D3D12
			CHECKHR(queue->Wait(fence, value));
			#endif
		}
		void CommandQueue::fence_wait_cpu(QueueFence& fence, u64 value)
		{
			vk::Result wait_result = sg_device.get_device().waitForFences(*fence.fence, true, std::numeric_limits<uint64_t>::max());
			seAssert(wait_result == vk::Result::eSuccess, "fence wait CPU error");
			sg_device.get_device().resetFences(*fence.fence);

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