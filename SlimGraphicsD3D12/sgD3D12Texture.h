#pragma once
#include "sgD3D12Buffer.h"

namespace sg
{
	namespace D3D12
	{
		class Texture : public Buffer
		{
			friend class Device;
		public:
			Texture() : Buffer(BufferType::Texture, false, false) { }
			
		};
	}
}
