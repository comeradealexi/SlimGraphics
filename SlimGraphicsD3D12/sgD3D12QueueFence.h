#pragma once
#include <sgTypes.h>
#include "sgD3D12Include.h"

namespace sg
{
	namespace D3D12
	{
		class QueueFence
		{
		public:
			ComPtr<ID3D12Fence> d3d12_fence;
		};
	}
}
