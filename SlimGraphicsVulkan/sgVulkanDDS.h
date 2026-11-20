#pragma once
#include <sgTypes.h>
#include <vector>

namespace sg
{
	namespace Vulkan
	{
		// Parsed DDS file. Subresources are ordered like D3D12 subresource indices: array slice major, then mip.
		struct DDSTextureData
		{
			struct Subresource
			{
				const uint8_t* data = nullptr;	// Points into the source file memory
				u32 row_pitch = 0;				// Tightly packed bytes per row (per block row for BC formats)
				u32 row_count = 0;				// Rows (block rows) * depth slices
			};

			ResourceCreateDesc desc;
			std::vector<Subresource> subresources;
		};

		// Supports the DX10 header and the common legacy formats (DXTn/ATIn/BCn FourCCs, 32/16/8 bit masks, float FourCCs)
		bool parse_dds(const void* dds_memory, size_t dds_size, DDSTextureData& out);
	}
}
