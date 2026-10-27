#pragma once
#include <sgTypes.h>

namespace sg
{
	namespace Vulkan
	{
		class IndexBufferView
		{
		public:
			D3D12_INDEX_BUFFER_VIEW ibv;
			SharedPtr<Buffer> buffer_resource;
		};
	}
}
