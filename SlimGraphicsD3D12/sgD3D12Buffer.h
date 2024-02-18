#pragma once
#include <sgTypes.h>
namespace sg
{
	namespace D3D12
	{
		class Buffer
		{
			friend class Device;
		public:
			ComPtr<ID3D12Resource> get() { return resource; }

		protected:
			SharedPtr<Memory> memory;
			ComPtr<ID3D12Resource> resource;
		};
	}
}
