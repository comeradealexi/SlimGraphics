#pragma once
#include <sgTypes.h>

namespace sg
{
	namespace Vulkan
	{
		class VertexBufferView
		{
		public:
			D3D12_VERTEX_BUFFER_VIEW vbv;
			SharedPtr<Buffer> buffer_resource;
		};
	}
}
