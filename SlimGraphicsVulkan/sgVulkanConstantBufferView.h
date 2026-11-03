#pragma once
#include <sgTypes.h>

namespace sg
{
	namespace Vulkan
	{
		class ConstantBufferView
		{
		public:
#if SG_PLATFORM_D3D12
			D3D12_CONSTANT_BUFFER_VIEW_DESC desc;
#endif
		};
	}
}
