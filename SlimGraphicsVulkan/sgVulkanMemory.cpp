#include "pch.h"
#include "sgVulkanMemory.h"
#include <D3D12MemAlloc.h>

namespace sg
{
	namespace Vulkan
	{


		Memory::Memory(MemoryType type_, D3D12MA::Allocation* alloc_) : type(type_), alloc(alloc_)
		{
		}
	}
}
