#include "pch.h"
#include "sgVulkanBuffer.h"	
#include "sgVulkanTypesTranslator.h"
#include "sgVulkanMemory.h"

namespace sg
{
	namespace Vulkan
	{


		sg::MemoryType Buffer::get_memory_type() const
		{
			return memory->get_type();
		}

		#if SG_PLATFORM_D3D12
		D3D12_RESOURCE_STATES Buffer::get_read_resource_state() const
		{
			return get_d3d12_resource_read_state(type, memory->get_type() == MemoryType::Readback);
		}
		#endif


		void* Buffer::map_memory()
		{
			seAssert(false, "VULKAN TODO " __FILE__ __FUNCTION__);
			return {};
			#if SG_PLATFORM_D3D12
			seAssert(cpu_writeable || cpu_readable, "Trying to write gpu memory that is not cpu visible");
			BYTE* pData;
			CHECKHR(resource->Map(0, nullptr, reinterpret_cast<void**>(&pData)));
			return (void*)pData;
			#endif //SG_PLATFORM_D3D12
		}

		void Buffer::unmap_memory()
		{
			seAssert(false, "VULKAN TODO " __FILE__ __FUNCTION__);
			#if SG_PLATFORM_D3D12
			resource->Unmap(0, nullptr);
			#endif
		}

		void Buffer::write_memory(u32 offset, const void* memory_src, u64 size)
		{
			seAssert(false, "VULKAN TODO " __FILE__ __FUNCTION__);
			#if SG_PLATFORM_D3D12
			seAssert(cpu_writeable, "Trying to write gpu memory that is not cpu visible");
			if (cpu_writeable)
			{
				BYTE* pData;
				CHECKHR(resource->Map(0, nullptr, reinterpret_cast<void**>(&pData)));
				memcpy(pData + offset, memory_src, size);
				resource->Unmap(0, nullptr);
			}
			#endif
		}

		void Buffer::read_memory(u32 offset, void* memory_dest, u64 size)
		{
			seAssert(false, "VULKAN TODO " __FILE__ __FUNCTION__);
			#if SG_PLATFORM_D3D12
			seAssert(cpu_readable, "Trying to write gpu memory that is not cpu visible");
			if (cpu_readable)
			{
				BYTE* pData;
				D3D12_RANGE readbackBufferRange{ 0, size };
				CHECKHR(resource->Map(0, &readbackBufferRange, reinterpret_cast<void**>(&pData)))
					memcpy(memory_dest, pData + offset, size);
				resource->Unmap(0, nullptr);
			}
			#endif
		}
	}
}