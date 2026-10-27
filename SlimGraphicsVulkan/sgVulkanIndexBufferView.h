#pragma once
#include <sgTypes.h>

namespace sg
{
	namespace D3D12
	{
		class IndexBufferView
		{
		public:
			D3D12_INDEX_BUFFER_VIEW ibv;
			SharedPtr<Buffer> buffer_resource;
		};
	}
}
