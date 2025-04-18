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
			SharedPtr<Buffer> buffer_resource;
			D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
			D3D12_UNORDERED_ACCESS_VIEW_DESC desc_uint;
		};
	}
}
