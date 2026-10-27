#pragma once
#include <sgTypes.h>
#include "sgVulkanTexture.h"
#include "sgVulkanDescriptorHeap.h"

namespace sg
{
	namespace Vulkan
	{
		class RenderTargetView
		{
		private:
			RenderTargetView(const RenderTargetView&) = delete;
			RenderTargetView& operator= (const RenderTargetView&) = delete;
		public:
			RenderTargetView() { }
			~RenderTargetView();

			u32 heap_index = DescriptorHeap::INVALID_HEAP_INDEX;
			CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle;
			SharedPtr<Texture> texture_resource;
			SharedPtr<DescriptorHeap> heap;
		};
	}
}
