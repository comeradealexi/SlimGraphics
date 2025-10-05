#pragma once
#include <sgTypes.h>
#include "sgD3D12Buffer.h"
#include "sgD3D12Texture.h"

namespace sg
{
	namespace D3D12
	{
		class UnorderedAccessView
		{
		public:
			SharedPtr<Buffer> buffer_resource;
			SharedPtr<Texture> texture_resource;
			D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
			D3D12_UNORDERED_ACCESS_VIEW_DESC desc_uint;

			inline ID3D12Resource* get_d3d12_resource()
			{
				if (buffer_resource) return buffer_resource->get().Get();
				if (texture_resource) return texture_resource->get().Get();
				return nullptr;
			}

			D3D12_RESOURCE_STATES get_d3d12_read_resource_state()
			{
				if (buffer_resource) return buffer_resource->get_read_resource_state();
				if (texture_resource) return texture_resource->get_read_resource_state();
				seAssert(false, "Should have a valid read state");
				return D3D12_RESOURCE_STATE_COMMON; 
			}
		};
	}
}
