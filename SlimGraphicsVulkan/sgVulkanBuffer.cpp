#include "pch.h"
#include "sgVulkanBuffer.h"
#include "sgVulkanMemory.h"

namespace sg
{
	namespace Vulkan
	{
		MemoryType Buffer::get_memory_type() const
		{
			return memory->get_type();
		}

		ResourceState Buffer::get_read_resource_state() const
		{
			switch (memory->get_type())
			{
			case MemoryType::Readback: return ResourceState::HostRead;
			case MemoryType::Upload: return ResourceState::CopySource; // Only ever a copy source; host writes are visible at submit
			default: return ResourceState::Read;
			}
		}

		void* Buffer::map_memory()
		{
			seAssert(cpu_writeable || cpu_readable, "Trying to map gpu memory that is not cpu visible");
			seAssert(memory->get_mapped_data() != nullptr, "Memory is not mapped");
			if (cpu_readable && !memory->is_host_coherent())
			{
				vmaInvalidateAllocation(memory->get_allocator(), memory->get_allocation(), 0, VK_WHOLE_SIZE);
			}
			return memory->get_mapped_data();
		}

		void Buffer::unmap_memory()
		{
			// Memory stays persistently mapped; just make CPU writes visible on non-coherent memory.
			if (cpu_writeable && !memory->is_host_coherent())
			{
				vmaFlushAllocation(memory->get_allocator(), memory->get_allocation(), 0, VK_WHOLE_SIZE);
			}
		}

		void Buffer::write_memory(u32 offset, const void* memory_src, u64 size)
		{
			seAssert(cpu_writeable, "Trying to write gpu memory that is not cpu visible");
			seAssert(offset + size <= size_bytes, "Write out of buffer bounds");
			if (cpu_writeable)
			{
				memcpy(static_cast<uint8_t*>(memory->get_mapped_data()) + offset, memory_src, size);
				if (!memory->is_host_coherent())
				{
					vmaFlushAllocation(memory->get_allocator(), memory->get_allocation(), offset, size);
				}
			}
		}

		void Buffer::read_memory(u32 offset, void* memory_dest, u64 size)
		{
			seAssert(cpu_readable, "Trying to read gpu memory that is not cpu visible");
			seAssert(offset + size <= size_bytes, "Read out of buffer bounds");
			if (cpu_readable)
			{
				if (!memory->is_host_coherent())
				{
					vmaInvalidateAllocation(memory->get_allocator(), memory->get_allocation(), offset, size);
				}
				memcpy(memory_dest, static_cast<const uint8_t*>(memory->get_mapped_data()) + offset, size);
			}
		}
	}
}
