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
			Memory(D3D12MA::Allocation* ptr);

			void write_memory(u32 offset, void* memory_src, u64 size);

			void read_memory(u32 offset, void* memory_dest, u64 size);

		private:
			AllocationPIMPL memory_ptr;
			bool cpu_writeable = false;
			bool cpu_readable = false;
		};
	}
}
