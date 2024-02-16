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
			~RenderTargetView();

			CD3DX12_CPU_DESCRIPTOR_HANDLE rtv;
			SharedPtr<Texture> texture_resource;
		};
	}
}
