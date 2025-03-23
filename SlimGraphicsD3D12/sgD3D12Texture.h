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
			Texture(bool uav_access) : Buffer(BufferType::Texture, 0, uav_access, false, false) { }
			
		};
	}
}
