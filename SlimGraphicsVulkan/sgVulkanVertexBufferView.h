#pragma once
#include <sgTypes.h>

namespace sg
{
	namespace Vulkan
	{
		class VertexBufferView
		{
		public:
			SharedPtr<Buffer> buffer_resource;
			u64 offset = 0;
			u64 size = 0;
			u64 stride = 0;
		};
	}
}
