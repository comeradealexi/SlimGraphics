#pragma once
#include "sgTypes.h"
#include "vk_mem_alloc.h"

namespace sg
{
	namespace Vulkan
	{
		class Memory
		{
			friend class Device;
		public:
#if SG_PLATFORM_D3D12
			Memory(MemoryType type_, D3D12MA::Allocation* alloc);
#endif
			MemoryType get_type() const { return type; }
		private:
			const MemoryType type;
#if SG_PLATFORM_D3D12
			D3D12MA::Allocation* alloc = nullptr;
#endif
		};
	}
}
