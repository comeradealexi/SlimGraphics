#include "pch.h"
#include "sgVulkanDDS.h"
#include "sgVulkanTypesTranslator.h"

namespace sg
{
	namespace Vulkan
	{
		namespace
		{
			constexpr u32 make_fourcc(char a, char b, char c, char d)
			{
				return static_cast<u32>(static_cast<uint8_t>(a)) | (static_cast<u32>(static_cast<uint8_t>(b)) << 8) | (static_cast<u32>(static_cast<uint8_t>(c)) << 16) | (static_cast<u32>(static_cast<uint8_t>(d)) << 24);
			}

			constexpr u32 kDDSMagic = make_fourcc('D', 'D', 'S', ' ');

			// DDS_PIXELFORMAT flags
			constexpr u32 DDPF_ALPHA = 0x2;
			constexpr u32 DDPF_FOURCC = 0x4;
			constexpr u32 DDPF_RGB = 0x40;
			constexpr u32 DDPF_LUMINANCE = 0x20000;
			// DDS_HEADER flags / caps
			constexpr u32 DDSD_DEPTH = 0x800000;
			constexpr u32 DDSCAPS2_CUBEMAP = 0x200;
			constexpr u32 DDSCAPS2_CUBEMAP_ALLFACES = 0xFC00;
			constexpr u32 DDSCAPS2_VOLUME = 0x200000;
			// DX10 header
			constexpr u32 DDS_DIMENSION_TEXTURE1D = 2;
			constexpr u32 DDS_DIMENSION_TEXTURE3D = 4;
			constexpr u32 DDS_RESOURCE_MISC_TEXTURECUBE = 0x4;

#pragma pack(push, 1)
			struct DDSPixelFormat
			{
				u32 size;
				u32 flags;
				u32 fourcc;
				u32 rgb_bit_count;
				u32 r_mask;
				u32 g_mask;
				u32 b_mask;
				u32 a_mask;
			};

			struct DDSHeader
			{
				u32 size;
				u32 flags;
				u32 height;
				u32 width;
				u32 pitch_or_linear_size;
				u32 depth;
				u32 mip_map_count;
				u32 reserved1[11];
				DDSPixelFormat ddspf;
				u32 caps;
				u32 caps2;
				u32 caps3;
				u32 caps4;
				u32 reserved2;
			};

			struct DDSHeaderDX10
			{
				u32 dxgi_format;
				u32 resource_dimension;
				u32 misc_flag;
				u32 array_size;
				u32 misc_flags2;
			};
#pragma pack(pop)
			static_assert(sizeof(DDSHeader) == 124, "DDS header size mismatch");
			static_assert(sizeof(DDSHeaderDX10) == 20, "DDS DX10 header size mismatch");

			bool masks_are(const DDSPixelFormat& pf, u32 r, u32 g, u32 b, u32 a)
			{
				return pf.r_mask == r && pf.g_mask == g && pf.b_mask == b && pf.a_mask == a;
			}

			// Same mapping rules as DirectXTK's DDSTextureLoader for the formats D3D12 can use directly
			DXGI_FORMAT get_legacy_format(const DDSPixelFormat& pf)
			{
				if (pf.flags & DDPF_FOURCC)
				{
					switch (pf.fourcc)
					{
					case make_fourcc('D', 'X', 'T', '1'): return DXGI_FORMAT_BC1_UNORM;
					case make_fourcc('D', 'X', 'T', '2'):
					case make_fourcc('D', 'X', 'T', '3'): return DXGI_FORMAT_BC2_UNORM;
					case make_fourcc('D', 'X', 'T', '4'):
					case make_fourcc('D', 'X', 'T', '5'): return DXGI_FORMAT_BC3_UNORM;
					case make_fourcc('A', 'T', 'I', '1'):
					case make_fourcc('B', 'C', '4', 'U'): return DXGI_FORMAT_BC4_UNORM;
					case make_fourcc('B', 'C', '4', 'S'): return DXGI_FORMAT_BC4_SNORM;
					case make_fourcc('A', 'T', 'I', '2'):
					case make_fourcc('B', 'C', '5', 'U'): return DXGI_FORMAT_BC5_UNORM;
					case make_fourcc('B', 'C', '5', 'S'): return DXGI_FORMAT_BC5_SNORM;
					case 36: return DXGI_FORMAT_R16G16B16A16_UNORM;
					case 110: return DXGI_FORMAT_R16G16B16A16_SNORM;
					case 111: return DXGI_FORMAT_R16_FLOAT;
					case 112: return DXGI_FORMAT_R16G16_FLOAT;
					case 113: return DXGI_FORMAT_R16G16B16A16_FLOAT;
					case 114: return DXGI_FORMAT_R32_FLOAT;
					case 115: return DXGI_FORMAT_R32G32_FLOAT;
					case 116: return DXGI_FORMAT_R32G32B32A32_FLOAT;
					default: return DXGI_FORMAT_UNKNOWN;
					}
				}
				if (pf.flags & DDPF_RGB)
				{
					switch (pf.rgb_bit_count)
					{
					case 32:
						if (masks_are(pf, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000)) return DXGI_FORMAT_R8G8B8A8_UNORM;
						if (masks_are(pf, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000)) return DXGI_FORMAT_B8G8R8A8_UNORM;
						if (masks_are(pf, 0x00ff0000, 0x0000ff00, 0x000000ff, 0)) return DXGI_FORMAT_B8G8R8A8_UNORM; // X8: alpha unused
						if (masks_are(pf, 0x3ff00000, 0x000ffc00, 0x000003ff, 0xc0000000)) return DXGI_FORMAT_R10G10B10A2_UNORM; // D3DX writes this swapped
						if (masks_are(pf, 0x000003ff, 0x000ffc00, 0x3ff00000, 0xc0000000)) return DXGI_FORMAT_R10G10B10A2_UNORM;
						if (masks_are(pf, 0x0000ffff, 0xffff0000, 0, 0)) return DXGI_FORMAT_R16G16_UNORM;
						if (masks_are(pf, 0xffffffff, 0, 0, 0)) return DXGI_FORMAT_R32_FLOAT;
						break;
					case 16:
						if (masks_are(pf, 0xf800, 0x07e0, 0x001f, 0)) return DXGI_FORMAT_B5G6R5_UNORM;
						if (masks_are(pf, 0x7c00, 0x03e0, 0x001f, 0x8000)) return DXGI_FORMAT_B5G5R5A1_UNORM;
						if (masks_are(pf, 0x0f00, 0x00f0, 0x000f, 0xf000)) return DXGI_FORMAT_B4G4R4A4_UNORM;
						break;
					default:
						break;
					}
					return DXGI_FORMAT_UNKNOWN;
				}
				if (pf.flags & DDPF_LUMINANCE)
				{
					if (pf.rgb_bit_count == 8) return DXGI_FORMAT_R8_UNORM;
					if (pf.rgb_bit_count == 16) return DXGI_FORMAT_R16_UNORM;
					return DXGI_FORMAT_UNKNOWN;
				}
				if ((pf.flags & DDPF_ALPHA) && pf.rgb_bit_count == 8)
				{
					return DXGI_FORMAT_A8_UNORM;
				}
				return DXGI_FORMAT_UNKNOWN;
			}
		}

		bool parse_dds(const void* dds_memory, size_t dds_size, DDSTextureData& out)
		{
			out = {};
			const uint8_t* bytes = static_cast<const uint8_t*>(dds_memory);
			if (dds_size < sizeof(u32) + sizeof(DDSHeader))
			{
				return false;
			}
			u32 magic = 0;
			memcpy(&magic, bytes, sizeof(magic));
			DDSHeader header;
			memcpy(&header, bytes + sizeof(u32), sizeof(header));
			if (magic != kDDSMagic || header.size != sizeof(DDSHeader) || header.ddspf.size != sizeof(DDSPixelFormat))
			{
				return false;
			}

			size_t data_offset = sizeof(u32) + sizeof(DDSHeader);
			ResourceCreateDesc& desc = out.desc;
			desc.width = std::max(1u, header.width);
			desc.height = std::max(1u, header.height);
			desc.mip_count = std::max(1u, header.mip_map_count);
			desc.depth = 1;
			desc.dimension = ResourceDimension::Texture2D;
			u32 array_size = 1;
			u32 depth = 1;

			if ((header.ddspf.flags & DDPF_FOURCC) && header.ddspf.fourcc == make_fourcc('D', 'X', '1', '0'))
			{
				if (dds_size < data_offset + sizeof(DDSHeaderDX10))
				{
					return false;
				}
				DDSHeaderDX10 dx10;
				memcpy(&dx10, bytes + data_offset, sizeof(dx10));
				data_offset += sizeof(DDSHeaderDX10);
				desc.format = static_cast<DXGI_FORMAT>(dx10.dxgi_format);
				array_size = std::max(1u, dx10.array_size);
				if (dx10.resource_dimension == DDS_DIMENSION_TEXTURE1D)
				{
					desc.dimension = ResourceDimension::Texture1D;
					desc.height = 1;
				}
				else if (dx10.resource_dimension == DDS_DIMENSION_TEXTURE3D)
				{
					desc.dimension = ResourceDimension::Texture3D;
					depth = std::max(1u, header.depth);
				}
				if (dx10.misc_flag & DDS_RESOURCE_MISC_TEXTURECUBE)
				{
					array_size *= 6;
				}
			}
			else
			{
				desc.format = get_legacy_format(header.ddspf);
				if (header.caps2 & DDSCAPS2_CUBEMAP)
				{
					if ((header.caps2 & DDSCAPS2_CUBEMAP_ALLFACES) != DDSCAPS2_CUBEMAP_ALLFACES)
					{
						return false; // Partial cube maps are not supported (also rejected by DDSTextureLoader)
					}
					array_size = 6;
				}
				else if ((header.flags & DDSD_DEPTH) && (header.caps2 & DDSCAPS2_VOLUME))
				{
					desc.dimension = ResourceDimension::Texture3D;
					depth = std::max(1u, header.depth);
				}
			}

			if (desc.format == DXGI_FORMAT_UNKNOWN)
			{
				seWriteLine("DDS: unsupported pixel format");
				return false;
			}
			desc.depth = desc.dimension == ResourceDimension::Texture3D ? depth : array_size;

			const FormatInfo info = get_format_info(desc.format);
			for (u32 slice = 0; slice < array_size; slice++)
			{
				for (u32 mip = 0; mip < desc.mip_count; mip++)
				{
					const u32 w = std::max(1u, desc.width >> mip);
					const u32 h = std::max(1u, desc.height >> mip);
					const u32 d = std::max(1u, depth >> mip);
					const u32 row_pitch = ((w + info.block_width - 1) / info.block_width) * info.block_bytes;
					const u32 rows = (h + info.block_height - 1) / info.block_height;
					const size_t surface_size = static_cast<size_t>(row_pitch) * rows * d;
					if (data_offset + surface_size > dds_size)
					{
						seWriteLine("DDS: file is truncated");
						return false;
					}
					out.subresources.push_back({ bytes + data_offset, row_pitch, rows * d });
					data_offset += surface_size;
				}
			}
			return true;
		}
	}
}
