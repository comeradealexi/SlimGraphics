#include "pch.h"
#include "sgD3D12Memory.h"
#include <D3D12MemAlloc.h>

namespace sg
{
	namespace D3D12
	{
		AllocationPIMPL::AllocationPIMPL() = default;
		AllocationPIMPL::~AllocationPIMPL() = default;

		Memory::Memory(MemoryType type_, D3D12MA::Allocation* ptr) : type(type_)
		{
			memory_ptr.ptr = ptr;
		}
	}
}
