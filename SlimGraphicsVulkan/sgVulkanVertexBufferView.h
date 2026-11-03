#pragma once
#include <sgTypes.h>

namespace sg
{
	namespace Vulkan
	{
		class VertexBufferView
		{
		public:
#if SG_PLATFORM_D3D12
			D3D12_VERTEX_BUFFER_VIEW vbv;
#endif
			SharedPtr<Buffer> buffer_resource;
		};
	}
}
