#pragma once
#include <sgTypes.h>
namespace sg
{
	namespace D3D12
	{
		class Buffer
		{
		public:

		protected:
			SharedPtr<Memory> memory;
			ComPtr<ID3D12Resource> resource;
		};
	}
}
