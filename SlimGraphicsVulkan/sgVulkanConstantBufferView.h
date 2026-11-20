#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"

namespace sg
{
	namespace Vulkan
	{
		// A range of a constant buffer, read by shaders straight from its device address (push address mapping). As with the
		// D3D12 GPU virtual address, the owner keeps the buffer alive.
		class ConstantBufferView
		{
		public:
			vk::DeviceAddress address = 0;
			u64 size = 0;
		};
	}
}
