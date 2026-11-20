#pragma once
#include <sgTypes.h>
#include "sgVulkanTexture.h"

namespace sg
{
	namespace Vulkan
	{
		// The attachment image view is fetched from the texture at use, so swap chain textures can change their image.
		class RenderTargetView
		{
		public:
			SharedPtr<Texture> texture_resource;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; // UNKNOWN = texture format
		};
	}
}
