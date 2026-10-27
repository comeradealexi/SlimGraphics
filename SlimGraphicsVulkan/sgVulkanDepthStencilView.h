#pragma once
#include <sgTypes.h>
#include "sgD3D12Texture.h"
#include "sgD3D12DescriptorHeap.h"

namespace sg
{
	namespace D3D12
	{
		class DepthStencilView
		{
		public:
			~DepthStencilView();

			u32 heap_index = DescriptorHeap::INVALID_HEAP_INDEX;
			CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle;
			SharedPtr<Texture> texture_resource;
			SharedPtr<DescriptorHeap> heap;
		};
	}
}
