#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"

namespace sg
{
	namespace Vulkan
	{
		// A VK_EXT_descriptor_heap heap: a host visible buffer that descriptors are written into, bound to command buffers with
		// vkCmdBindResourceHeapEXT / vkCmdBindSamplerHeapEXT. The implementation reserved range sits at the end of the buffer.
		class DescriptorHeap
		{
		public:
			struct CreateDesc
			{
				u64 usable_size = 0;			// Bytes available to the application (from offset 0)
				u64 reserved_size = 0;			// Implementation reserved range size (min*HeapReservedRange)
				u64 reserved_alignment = 1;		// Alignment of the reserved range offset (descriptor alignment)
				u64 heap_alignment = 1;			// samplerHeapAlignment / resourceHeapAlignment
				const char* name = "";
			};

			DescriptorHeap(const vk::raii::Device& device, VmaAllocator allocator, const CreateDesc& desc);
			~DescriptorHeap();
			DescriptorHeap(const DescriptorHeap&) = delete;
			DescriptorHeap& operator=(const DescriptorHeap&) = delete;

			uint8_t* get_mapped_data() const { return mapped_data; }
			u64 get_usable_size() const { return usable_size; }
			const vk::BindHeapInfoEXT& get_bind_info() const { return bind_info; }

		private:
			VmaAllocator allocator = VK_NULL_HANDLE;
			VkBuffer buffer = VK_NULL_HANDLE;
			VmaAllocation allocation = VK_NULL_HANDLE;
			uint8_t* mapped_data = nullptr;
			u64 usable_size = 0;
			vk::BindHeapInfoEXT bind_info;
		};
	}
}
