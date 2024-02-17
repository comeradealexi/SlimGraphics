#pragma once
#include <sgTypes.h>
#include "sgD3D12Texture.h"

namespace sg
{
	namespace D3D12
	{
		class DepthStencilView
		{
		public:
			~DepthStencilView();

			DSVBinding dsv;
			SharedPtr<Texture> texture_resource;
		};
	}
}
