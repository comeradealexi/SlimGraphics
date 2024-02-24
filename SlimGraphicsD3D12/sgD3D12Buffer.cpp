#include "pch.h"
#include "sgD3D12Buffer.h"	

namespace sg
{
	namespace D3D12
	{
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