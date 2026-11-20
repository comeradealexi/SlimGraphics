#pragma once
#include <sgTypes.h>
#include "sgVulkanTexture.h"

namespace sg
{
	namespace Vulkan
	{
		class DepthStencilView
		{
		public:
			SharedPtr<Texture> texture_resource;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; // UNKNOWN = texture format
		};
	}
}
