#pragma once
#include <sgTypes.h>
#include "sgD3D12Buffer.h"

namespace sg
{
	namespace D3D12
	{
		class UnorderedAccessView
		{
		public:
			UAVBinding uav;
			SharedPtr<Buffer> buffer_resource;
		};
	}
}
