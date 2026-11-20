#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"

namespace sg
{
	namespace Vulkan
	{
		// D3D12 fence semantics (a monotonically increasing 64-bit value) map directly onto a Vulkan timeline semaphore.
		class QueueFence
		{
		public:
			vk::raii::Semaphore timeline_semaphore = nullptr;
		};
	}
}
