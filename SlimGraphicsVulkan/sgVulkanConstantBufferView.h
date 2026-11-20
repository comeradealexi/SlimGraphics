#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"

namespace sg
{
	namespace Vulkan
	{
		// A range of a constant buffer. As with the D3D12 GPU virtual address, the owner keeps the buffer alive.
		class ConstantBufferView
		{
		public:
			vk::Buffer buffer = VK_NULL_HANDLE;
			u64 offset = 0;
			u64 size = 0;
		};
	}
}
