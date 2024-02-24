#pragma once
#include "sgTypes.h"

namespace sg
{
	namespace D3D12
	{
		struct AllocationPIMPL
		{
			AllocationPIMPL();
			~AllocationPIMPL();
			inline D3D12MA::Allocation* operator->()
			{
				return ptr.Get();
			}
			ComPtr<D3D12MA::Allocation> ptr;
			ComPtr<D3D12MA::Pool> ptr_pool; //Usually nullptr - Only if the memory allocation owns the pool
		};

		class Memory
		{
			friend class Device;
		public:
			Memory(MemoryType type_, D3D12MA::Allocation* ptr);
			MemoryType get_type() const { return type; }
		private:
			const MemoryType type;
			AllocationPIMPL memory_ptr;
		};
	}
}
