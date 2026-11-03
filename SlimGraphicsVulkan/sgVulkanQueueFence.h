#pragma once

namespace sg
{
	namespace Vulkan
	{
		class QueueFence
		{
		public:
			vk::raii::Fence fence;
		};
	}
}
