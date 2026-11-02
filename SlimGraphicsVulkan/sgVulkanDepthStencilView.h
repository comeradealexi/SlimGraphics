#pragma once
#include <sgTypes.h>
#include "sgVulkanTexture.h"
#include "sgVulkanDescriptorHeap.h"

namespace sg
{
	namespace Vulkan
	{
		class DepthStencilView
		{
		public:
			~DepthStencilView();

			u32 heap_index = DescriptorHeap::INVALID_HEAP_INDEX;
#if SG_PLATFORM_D3D12
			CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle;
#endif
			SharedPtr<Texture> texture_resource;
			SharedPtr<DescriptorHeap> heap;
		};
	}
}
