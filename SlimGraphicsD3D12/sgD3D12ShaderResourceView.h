#pragma once
#include <sgTypes.h>

namespace sg
{
	namespace D3D12
	{
		class ShaderResourceView
		{
		public:
			SharedPtr<Buffer> buffer_resource;
			D3D12_SHADER_RESOURCE_VIEW_DESC desc;
		};
	}
}
