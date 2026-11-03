#include "pch.h"
#include "sgVulkanMemory.h"

namespace sg
{
	namespace Vulkan
	{

#if SG_PLATFORM_D3D12
		Memory::Memory(MemoryType type_, D3D12MA::Allocation* alloc_) : type(type_), alloc(alloc_)
		{
		}
#endif
	}
}
