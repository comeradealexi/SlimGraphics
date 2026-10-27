#include "pch.h"
#include "sgVulkanRenderTargetView.h"
#include "sgVulkanDevice.h"

namespace sg
{
	namespace Vulkan
	{
		RenderTargetView::~RenderTargetView()
		{
			heap->free(heap_index);
		}
	}
}