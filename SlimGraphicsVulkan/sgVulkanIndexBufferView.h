#pragma once
#include <sgTypes.h>

namespace sg
{
	namespace Vulkan
	{
		class IndexBufferView
		{
		public:
#if SG_PLATFORM_D3D12
			D3D12_INDEX_BUFFER_VIEW ibv;
#endif
			SharedPtr<Buffer> buffer_resource;
		};
	}
}
