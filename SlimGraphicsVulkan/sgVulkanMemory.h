#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"

namespace sg
{
	namespace Vulkan
	{
		// A block of device memory that buffers/textures are bound into (the Vulkan equivalent of the D3D12 backend's
		// D3D12MA heap allocation that placed resources are created in).
		class Memory
		{
		public:
			Memory(MemoryType type, VmaAllocator allocator, VmaAllocation allocation);
			~Memory();
			Memory(const Memory&) = delete;
			Memory& operator=(const Memory&) = delete;

			MemoryType get_type() const { return type; }
			VmaAllocator get_allocator() const { return allocator; }
			VmaAllocation get_allocation() const { return allocation; }
			u64 get_size() const { return size; }
			u32 get_memory_type_index() const { return memory_type_index; }

			// Upload and readback memory is persistently mapped
			void* get_mapped_data() const { return mapped_data; }
			bool is_host_coherent() const { return host_coherent; }

		private:
			const MemoryType type;
			VmaAllocator allocator = VK_NULL_HANDLE;
			VmaAllocation allocation = VK_NULL_HANDLE;
			void* mapped_data = nullptr;
			u64 size = 0;
			u32 memory_type_index = 0;
			bool host_coherent = true;
		};
	}
}
