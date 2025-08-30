#pragma once
#include "sgTypes.h"
#include "D3D12MemAlloc.h"

namespace sg
{
	namespace D3D12
	{
		class Memory
		{
			friend class Device;
		public:
			Memory(MemoryType type_, D3D12MA::Allocation* alloc);
			MemoryType get_type() const { return type; }
		private:
			const MemoryType type;
			D3D12MA::Allocation* alloc = nullptr;
		};
	}
}
