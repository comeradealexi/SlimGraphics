#pragma once
#include <sgTypes.h>
#include "sgD3D12Texture.h"

namespace sg
{
	namespace D3D12
	{
		class RenderTargetView
		{
		public:
			RenderTargetView(SharedPtr<Texture> texture);

		private:
			SharedPtr<Texture> texture_resource;
		};
	}
}
