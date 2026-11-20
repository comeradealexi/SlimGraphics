#pragma once
#include <sgTypes.h>

namespace sg
{
	namespace Vulkan
	{
		// Either a structured/raw buffer range or a texture mip range. Image views are created (and cached) by the texture.
		class ShaderResourceView
		{
		public:
			SharedPtr<Texture> texture_resource;
			SharedPtr<Buffer> buffer_resource;
			u64 element_size = 0;
			u64 element_count = 0;
			u32 top_mip = 0;
			u32 mip_count = static_cast<u32>(-1);
		};
	}
}
