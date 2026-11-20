#include "pch.h"
#include "sgVulkanDescriptorHeap.h"
#include "sgUtils.h"

namespace sg
{
	namespace Vulkan
	{
		DescriptorHeap::DescriptorHeap(const vk::raii::Device& device, VmaAllocator allocator_, const CreateDesc& desc) : allocator(allocator_), usable_size(desc.usable_size)
		{
			const u64 reserved_offset = AlignUp(desc.usable_size, desc.reserved_alignment);
			const u64 total_size = reserved_offset + desc.reserved_size;

			VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			buffer_info.size = total_size;
			buffer_info.usage = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			// Written by the CPU every frame and read by the GPU: host visible, preferably device local (resizable BAR)
			VmaAllocationCreateInfo allocation_info = {};
			allocation_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			allocation_info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			allocation_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

			VmaAllocationInfo info = {};
			CHECKVK(vmaCreateBufferWithAlignment(allocator, &buffer_info, &allocation_info, desc.heap_alignment, &buffer, &allocation, &info));
			mapped_data = static_cast<uint8_t*>(info.pMappedData);
			vmaSetAllocationName(allocator, allocation, desc.name);

			const vk::DeviceAddress address = device.getBufferAddress(vk::BufferDeviceAddressInfo(buffer));
			seAssert(address % desc.heap_alignment == 0, "Descriptor heap address is not aligned");
			bind_info.heapRange = vk::DeviceAddressRangeEXT(address, total_size);
			bind_info.reservedRangeOffset = desc.reserved_size ? reserved_offset : 0;
			bind_info.reservedRangeSize = desc.reserved_size;
		}

		DescriptorHeap::~DescriptorHeap()
		{
			if (buffer)
			{
				vmaDestroyBuffer(allocator, buffer, allocation);
			}
		}
	}
}
