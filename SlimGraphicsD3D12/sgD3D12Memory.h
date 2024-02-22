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
		};

		class Memory
		{
			friend class Device;
		public:
			Memory(D3D12MA::Allocation* ptr);

		private:
			AllocationPIMPL memory_ptr;
		};
	}
}
