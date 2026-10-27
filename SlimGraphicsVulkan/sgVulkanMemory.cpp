#include "pch.h"
#include "sgD3D12Memory.h"
#include <D3D12MemAlloc.h>

namespace sg
{
	namespace D3D12
	{


		Memory::Memory(MemoryType type_, D3D12MA::Allocation* alloc_) : type(type_), alloc(alloc_)
		{
		}
	}
}
