#pragma once
#include <sgTypes.h>
#include "sgVulkanBuffer.h"
#include "sgVulkanTexture.h"

namespace sg
{
	namespace Vulkan
	{
		class UnorderedAccessView
		{
		public:
			SharedPtr<Buffer> buffer_resource;
			SharedPtr<Texture> texture_resource;
			u64 element_size = 0;
			u64 element_count = 0;
			u32 mip_level = 0;

			explicit operator bool() const
			{
				return buffer_resource || texture_resource;
			}

			// Identifies the underlying resource (barriers are tracked per resource, not per view)
			const void* get_resource_identity() const
			{
				if (buffer_resource) return buffer_resource.get();
				return texture_resource.get();
			}

			u64 get_size_bytes() const { return element_size * element_count; }
		};
	}
}
