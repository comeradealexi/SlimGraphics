#pragma once
#include "sgD3D12Buffer.h"

namespace sg
{
	namespace D3D12
	{
		class Texture : public Buffer
		{
		public:
			
		private:
			ComPtr<ID3D12Resource> resource;
		};
	}
}
