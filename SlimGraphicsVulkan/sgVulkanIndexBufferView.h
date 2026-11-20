#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"

namespace sg
{
	namespace Vulkan
	{
		class IndexBufferView
		{
		public:
			SharedPtr<Buffer> buffer_resource;
			u64 offset = 0;
			u64 size = 0;
			vk::IndexType index_type = vk::IndexType::eUint32;
		};
	}
}
