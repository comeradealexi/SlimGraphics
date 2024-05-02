#pragma once
#include "sgTypes.h"
#include "sgD3D12Include.h"

namespace sg
{
	namespace D3D12
	{
		struct Allocation
		{
			uint64_t offset = 0;
			uint64_t virtual_allocation = {};
			ComPtr<D3D12MA::VirtualBlock> virtual_block;
			ComPtr<ID3D12Heap> heap;
			inline D3D12MA::VirtualAllocation* virtual_allocation_cast()
			{
				return (D3D12MA::VirtualAllocation*)&virtual_allocation;
			}
		};

		struct AllocationPIMPL
		{
			AllocationPIMPL();
			~AllocationPIMPL();
			inline Allocation& operator->()
			{
				return alloc;
			}
			Allocation alloc;
		};

		class Memory
		{
			friend class Device;
		public:
			Memory(MemoryType type_, Allocation alloc);
			MemoryType get_type() const { return type; }
		private:
			const MemoryType type;
			AllocationPIMPL memory;
		};
	}
}
