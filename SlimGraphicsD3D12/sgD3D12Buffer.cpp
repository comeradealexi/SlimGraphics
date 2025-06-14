#include "pch.h"
#include "sgD3D12Buffer.h"	
#include "sgD3D12TypesTranslator.h"
#include "sgD3D12Memory.h"

namespace sg
{
	namespace D3D12
	{


		sg::MemoryType Buffer::get_memory_type() const
		{
			return memory->get_type();
		}

		D3D12_RESOURCE_STATES Buffer::get_read_resource_state() const
		{
			return get_d3d12_resource_read_state(type, memory->get_type() == MemoryType::Readback);
		}

		void Buffer::write_memory(u32 offset, const void* memory_src, u64 size)
		{
			seAssert(cpu_writeable, "Trying to write gpu memory that is not cpu visible");
			if (cpu_writeable)
			{
				BYTE* pData;
				CHECKHR(resource->Map(0, nullptr, reinterpret_cast<void**>(&pData)))
					memcpy(pData + offset, memory_src, size);
				resource->Unmap(0, nullptr);
			}
		}

		void Buffer::read_memory(u32 offset, void* memory_dest, u64 size)
		{
			seAssert(cpu_readable, "Trying to write gpu memory that is not cpu visible");
			if (cpu_readable)
			{
				BYTE* pData;
				CHECKHR(resource->Map(0, nullptr, reinterpret_cast<void**>(&pData)))
					memcpy(memory_dest, pData + offset, size);
				resource->Unmap(0, nullptr);
			}
		}
	}
}