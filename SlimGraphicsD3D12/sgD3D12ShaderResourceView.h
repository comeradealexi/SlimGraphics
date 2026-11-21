#pragma once
#include <sgTypes.h>

namespace sg
{
	namespace D3D12
	{
		class ShaderResourceView
		{
		public:
			SharedPtr<Texture> texture_resource;
			SharedPtr<Buffer> buffer_resource;
			D3D12_SHADER_RESOURCE_VIEW_DESC desc;
			D3D12_SHADER_RESOURCE_VIEW_DESC desc_uint;
			bool acceleration_structure = false; // The view is created without a resource (its desc holds the GPU address)

			// The resource the view is created with (nullptr for acceleration structures)
			ID3D12Resource* get_view_resource() const;
		};
	}
}
