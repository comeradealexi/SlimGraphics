#include "pch.h"
#include "sgVulkanDepthStencilView.h"
#include "sgVulkanDevice.h"

namespace sg
{
	namespace Vulkan
	{
		DepthStencilView::~DepthStencilView()
		{
			heap->free(heap_index);
		}
	}
}