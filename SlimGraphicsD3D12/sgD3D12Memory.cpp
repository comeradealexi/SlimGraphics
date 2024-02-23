#include "pch.h"
#include "sgD3D12Memory.h"
#include <D3D12MemAlloc.h>

namespace sg
{
	namespace D3D12
	{
		AllocationPIMPL::AllocationPIMPL() = default;
		AllocationPIMPL::~AllocationPIMPL() = default;

		Memory::Memory(D3D12MA::Allocation* ptr)
		{
			memory_ptr.ptr = ptr;
		}

		void Memory::write_memory(u32 offset, void* memory_src, u64 size)
		{
			seAssert(cpu_writeable, "Trying to write gpu memory that is not cpu visible");
			if (cpu_writeable)
			{
				BYTE* pData;
				CHECKHR(memory_ptr.ptr->GetResource()->Map(0, nullptr, reinterpret_cast<void**>(&pData)))
				memcpy(pData + offset, memory_src, size);
				memory_ptr.ptr->GetResource()->Unmap(0, nullptr);
			}
		}

		void Memory::read_memory(u32 offset, void* memory_dest, u64 size)
		{
			seAssert(cpu_readable, "Trying to write gpu memory that is not cpu visible");
			if (cpu_readable)
			{
				BYTE* pData;
				CHECKHR(memory_ptr.ptr->GetResource()->Map(0, nullptr, reinterpret_cast<void**>(&pData)))
				memcpy(memory_dest, pData + offset, size);
				memory_ptr.ptr->GetResource()->Unmap(0, nullptr);
			}
		}

	}
}
