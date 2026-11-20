#include "pch.h"
#include "sgVulkanMemory.h"

namespace sg
{
	namespace Vulkan
	{
		Memory::Memory(MemoryType type_, VmaAllocator allocator_, VmaAllocation allocation_) : type(type_), allocator(allocator_), allocation(allocation_)
		{
			VmaAllocationInfo info = {};
			vmaGetAllocationInfo(allocator, allocation, &info);
			mapped_data = info.pMappedData;
			size = info.size;
			memory_type_index = info.memoryType;

			VkMemoryPropertyFlags flags = 0;
			vmaGetMemoryTypeProperties(allocator, memory_type_index, &flags);
			host_coherent = (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
		}

		Memory::~Memory()
		{
			// Resources bound to this memory hold a SharedPtr to it, so they are already destroyed.
			if (allocation)
			{
				vmaFreeMemory(allocator, allocation);
				allocation = VK_NULL_HANDLE;
			}
		}
	}
}
