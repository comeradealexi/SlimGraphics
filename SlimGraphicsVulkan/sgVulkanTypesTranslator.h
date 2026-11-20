#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"

namespace sg
{
	namespace Vulkan
	{
		// Size information for a format: a block is 1x1 pixel for uncompressed formats and 4x4 for BC formats.
		struct FormatInfo
		{
			u32 block_bytes = 0;
			u32 block_width = 1;
			u32 block_height = 1;
		};

		inline vk::Format translate(DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_UNKNOWN:					return vk::Format::eUndefined;
			case DXGI_FORMAT_R32G32B32A32_FLOAT:		return vk::Format::eR32G32B32A32Sfloat;
			case DXGI_FORMAT_R32G32B32A32_UINT:			return vk::Format::eR32G32B32A32Uint;
			case DXGI_FORMAT_R32G32B32A32_SINT:			return vk::Format::eR32G32B32A32Sint;
			case DXGI_FORMAT_R32G32B32_FLOAT:			return vk::Format::eR32G32B32Sfloat;
			case DXGI_FORMAT_R32G32B32_UINT:			return vk::Format::eR32G32B32Uint;
			case DXGI_FORMAT_R32G32B32_SINT:			return vk::Format::eR32G32B32Sint;
			case DXGI_FORMAT_R16G16B16A16_FLOAT:		return vk::Format::eR16G16B16A16Sfloat;
			case DXGI_FORMAT_R16G16B16A16_UNORM:		return vk::Format::eR16G16B16A16Unorm;
			case DXGI_FORMAT_R16G16B16A16_UINT:			return vk::Format::eR16G16B16A16Uint;
			case DXGI_FORMAT_R16G16B16A16_SNORM:		return vk::Format::eR16G16B16A16Snorm;
			case DXGI_FORMAT_R16G16B16A16_SINT:			return vk::Format::eR16G16B16A16Sint;
			case DXGI_FORMAT_R32G32_FLOAT:				return vk::Format::eR32G32Sfloat;
			case DXGI_FORMAT_R32G32_UINT:				return vk::Format::eR32G32Uint;
			case DXGI_FORMAT_R32G32_SINT:				return vk::Format::eR32G32Sint;
			case DXGI_FORMAT_R10G10B10A2_UNORM:			return vk::Format::eA2B10G10R10UnormPack32;
			case DXGI_FORMAT_R10G10B10A2_UINT:			return vk::Format::eA2B10G10R10UintPack32;
			case DXGI_FORMAT_R11G11B10_FLOAT:			return vk::Format::eB10G11R11UfloatPack32;
			case DXGI_FORMAT_R8G8B8A8_UNORM:			return vk::Format::eR8G8B8A8Unorm;
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:		return vk::Format::eR8G8B8A8Srgb;
			case DXGI_FORMAT_R8G8B8A8_UINT:				return vk::Format::eR8G8B8A8Uint;
			case DXGI_FORMAT_R8G8B8A8_SNORM:			return vk::Format::eR8G8B8A8Snorm;
			case DXGI_FORMAT_R8G8B8A8_SINT:				return vk::Format::eR8G8B8A8Sint;
			case DXGI_FORMAT_B8G8R8A8_UNORM:			return vk::Format::eB8G8R8A8Unorm;
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:		return vk::Format::eB8G8R8A8Srgb;
			case DXGI_FORMAT_R16G16_FLOAT:				return vk::Format::eR16G16Sfloat;
			case DXGI_FORMAT_R16G16_UNORM:				return vk::Format::eR16G16Unorm;
			case DXGI_FORMAT_R16G16_UINT:				return vk::Format::eR16G16Uint;
			case DXGI_FORMAT_R16G16_SNORM:				return vk::Format::eR16G16Snorm;
			case DXGI_FORMAT_R16G16_SINT:				return vk::Format::eR16G16Sint;
			case DXGI_FORMAT_D32_FLOAT:					return vk::Format::eD32Sfloat;
			case DXGI_FORMAT_R32_FLOAT:					return vk::Format::eR32Sfloat;
			case DXGI_FORMAT_R32_UINT:					return vk::Format::eR32Uint;
			case DXGI_FORMAT_R32_SINT:					return vk::Format::eR32Sint;
			case DXGI_FORMAT_D24_UNORM_S8_UINT:			return vk::Format::eD24UnormS8Uint;
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:		return vk::Format::eD32SfloatS8Uint;
			case DXGI_FORMAT_R8G8_UNORM:				return vk::Format::eR8G8Unorm;
			case DXGI_FORMAT_R8G8_UINT:					return vk::Format::eR8G8Uint;
			case DXGI_FORMAT_R8G8_SNORM:				return vk::Format::eR8G8Snorm;
			case DXGI_FORMAT_R8G8_SINT:					return vk::Format::eR8G8Sint;
			case DXGI_FORMAT_R16_FLOAT:					return vk::Format::eR16Sfloat;
			case DXGI_FORMAT_D16_UNORM:					return vk::Format::eD16Unorm;
			case DXGI_FORMAT_R16_UNORM:					return vk::Format::eR16Unorm;
			case DXGI_FORMAT_R16_UINT:					return vk::Format::eR16Uint;
			case DXGI_FORMAT_R16_SNORM:					return vk::Format::eR16Snorm;
			case DXGI_FORMAT_R16_SINT:					return vk::Format::eR16Sint;
			case DXGI_FORMAT_R8_UNORM:					return vk::Format::eR8Unorm;
			case DXGI_FORMAT_R8_UINT:					return vk::Format::eR8Uint;
			case DXGI_FORMAT_R8_SNORM:					return vk::Format::eR8Snorm;
			case DXGI_FORMAT_R8_SINT:					return vk::Format::eR8Sint;
			case DXGI_FORMAT_A8_UNORM:					return vk::Format::eR8Unorm; // Swizzled to .a in the view
			case DXGI_FORMAT_BC1_UNORM:					return vk::Format::eBc1RgbaUnormBlock;
			case DXGI_FORMAT_BC1_UNORM_SRGB:			return vk::Format::eBc1RgbaSrgbBlock;
			case DXGI_FORMAT_BC2_UNORM:					return vk::Format::eBc2UnormBlock;
			case DXGI_FORMAT_BC2_UNORM_SRGB:			return vk::Format::eBc2SrgbBlock;
			case DXGI_FORMAT_BC3_UNORM:					return vk::Format::eBc3UnormBlock;
			case DXGI_FORMAT_BC3_UNORM_SRGB:			return vk::Format::eBc3SrgbBlock;
			case DXGI_FORMAT_BC4_UNORM:					return vk::Format::eBc4UnormBlock;
			case DXGI_FORMAT_BC4_SNORM:					return vk::Format::eBc4SnormBlock;
			case DXGI_FORMAT_BC5_UNORM:					return vk::Format::eBc5UnormBlock;
			case DXGI_FORMAT_BC5_SNORM:					return vk::Format::eBc5SnormBlock;
			case DXGI_FORMAT_BC6H_UF16:					return vk::Format::eBc6HUfloatBlock;
			case DXGI_FORMAT_BC6H_SF16:					return vk::Format::eBc6HSfloatBlock;
			case DXGI_FORMAT_BC7_UNORM:					return vk::Format::eBc7UnormBlock;
			case DXGI_FORMAT_BC7_UNORM_SRGB:			return vk::Format::eBc7SrgbBlock;
			case DXGI_FORMAT_B5G6R5_UNORM:				return vk::Format::eR5G6B5UnormPack16;
			case DXGI_FORMAT_B5G5R5A1_UNORM:			return vk::Format::eA1R5G5B5UnormPack16;
			case DXGI_FORMAT_B4G4R4A4_UNORM:			return vk::Format::eA4R4G4B4UnormPack16;
			default:
				seAssert(false, "Unsupported DXGI_FORMAT %u for Vulkan", static_cast<u32>(format));
				return vk::Format::eUndefined;
			}
		}

		// Maps a Vulkan format back to DXGI (used for swap chain formats picked by the surface)
		inline DXGI_FORMAT translate(vk::Format format)
		{
			switch (format)
			{
			case vk::Format::eR8G8B8A8Unorm:	return DXGI_FORMAT_R8G8B8A8_UNORM;
			case vk::Format::eR8G8B8A8Srgb:		return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			case vk::Format::eB8G8R8A8Unorm:	return DXGI_FORMAT_B8G8R8A8_UNORM;
			case vk::Format::eB8G8R8A8Srgb:		return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
			case vk::Format::eA2B10G10R10UnormPack32: return DXGI_FORMAT_R10G10B10A2_UNORM;
			case vk::Format::eR16G16B16A16Sfloat: return DXGI_FORMAT_R16G16B16A16_FLOAT;
			default:
				seAssert(false, "Unsupported vk::Format %i", static_cast<int>(format));
				return DXGI_FORMAT_UNKNOWN;
			}
		}

		inline FormatInfo get_format_info(DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT:
				return { 16 };
			case DXGI_FORMAT_R32G32B32_FLOAT: case DXGI_FORMAT_R32G32B32_UINT: case DXGI_FORMAT_R32G32B32_SINT:
				return { 12 };
			case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_UINT:
			case DXGI_FORMAT_R16G16B16A16_SNORM: case DXGI_FORMAT_R16G16B16A16_SINT: case DXGI_FORMAT_R32G32_FLOAT:
			case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
				return { 8 };
			case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_UINT: case DXGI_FORMAT_R11G11B10_FLOAT:
			case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_UINT:
			case DXGI_FORMAT_R8G8B8A8_SNORM: case DXGI_FORMAT_R8G8B8A8_SINT: case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_UNORM:
			case DXGI_FORMAT_R16G16_UINT: case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R16G16_SINT:
			case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT:
			case DXGI_FORMAT_D24_UNORM_S8_UINT:
				return { 4 };
			case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R8G8_UINT: case DXGI_FORMAT_R8G8_SNORM: case DXGI_FORMAT_R8G8_SINT:
			case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_UINT:
			case DXGI_FORMAT_R16_SNORM: case DXGI_FORMAT_R16_SINT: case DXGI_FORMAT_B5G6R5_UNORM: case DXGI_FORMAT_B5G5R5A1_UNORM:
			case DXGI_FORMAT_B4G4R4A4_UNORM:
				return { 2 };
			case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_SNORM: case DXGI_FORMAT_R8_SINT:
			case DXGI_FORMAT_A8_UNORM:
				return { 1 };
			case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM:
				return { 8, 4, 4 };
			case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
			case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
			case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB:
				return { 16, 4, 4 };
			default:
				seAssert(false, "Missing format info for DXGI_FORMAT %u", static_cast<u32>(format));
				return { 4 };
			}
		}

		inline bool is_depth_format(DXGI_FORMAT format)
		{
			return format == DXGI_FORMAT_D32_FLOAT || format == DXGI_FORMAT_D16_UNORM || format == DXGI_FORMAT_D24_UNORM_S8_UINT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		}

		inline bool has_stencil(DXGI_FORMAT format)
		{
			return format == DXGI_FORMAT_D24_UNORM_S8_UINT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		}

		inline vk::ImageAspectFlags get_aspect_flags(DXGI_FORMAT format)
		{
			if (!is_depth_format(format))
			{
				return vk::ImageAspectFlagBits::eColor;
			}
			return has_stencil(format) ? (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil) : vk::ImageAspectFlagBits::eDepth;
		}

		inline vk::CullModeFlags translate(Rasterizer::CullMode cull_mode)
		{
			switch (cull_mode)
			{
			case Rasterizer::CullMode::None: return vk::CullModeFlagBits::eNone;
			case Rasterizer::CullMode::Front: return vk::CullModeFlagBits::eFront;
			case Rasterizer::CullMode::Back: return vk::CullModeFlagBits::eBack;
			}
			seAssert(false, "missing cull mode");
			return vk::CullModeFlagBits::eBack;
		}

		inline vk::PolygonMode translate(Rasterizer::FillMode fill_mode)
		{
			return fill_mode == Rasterizer::FillMode::Wireframe ? vk::PolygonMode::eLine : vk::PolygonMode::eFill;
		}

		// D3D12_COMPARISON_FUNC_NONE has no Vulkan equivalent; it is only meaningful for samplers where it disables comparison.
		inline vk::CompareOp translate(ComparisonFunction comp_func)
		{
			switch (comp_func)
			{
			case ComparisonFunction::None: return vk::CompareOp::eNever;
			case ComparisonFunction::Never: return vk::CompareOp::eNever;
			case ComparisonFunction::Less: return vk::CompareOp::eLess;
			case ComparisonFunction::Equal: return vk::CompareOp::eEqual;
			case ComparisonFunction::LessEqual: return vk::CompareOp::eLessOrEqual;
			case ComparisonFunction::Greater: return vk::CompareOp::eGreater;
			case ComparisonFunction::NotEqual: return vk::CompareOp::eNotEqual;
			case ComparisonFunction::GreaterEqual: return vk::CompareOp::eGreaterOrEqual;
			case ComparisonFunction::Always: return vk::CompareOp::eAlways;
			}
			seAssert(false, "missing comparison function");
			return vk::CompareOp::eAlways;
		}

		inline vk::StencilOp translate(DepthStencil::StencilOperation stencil_op)
		{
			switch (stencil_op)
			{
			case DepthStencil::StencilOperation::Keep: return vk::StencilOp::eKeep;
			case DepthStencil::StencilOperation::Zero: return vk::StencilOp::eZero;
			case DepthStencil::StencilOperation::Replace: return vk::StencilOp::eReplace;
			case DepthStencil::StencilOperation::IncreaseClamp: return vk::StencilOp::eIncrementAndClamp;
			case DepthStencil::StencilOperation::DecreaseClamp: return vk::StencilOp::eDecrementAndClamp;
			case DepthStencil::StencilOperation::Invert: return vk::StencilOp::eInvert;
			case DepthStencil::StencilOperation::IncreaseWrap: return vk::StencilOp::eIncrementAndWrap;
			case DepthStencil::StencilOperation::DecreaseWrap: return vk::StencilOp::eDecrementAndWrap;
			}
			seAssert(false, "missing stencil operation");
			return vk::StencilOp::eKeep;
		}

		inline vk::PrimitiveTopology translate(PrimitiveTopology topology)
		{
			switch (topology)
			{
			case PrimitiveTopology::Pointlist: return vk::PrimitiveTopology::ePointList;
			case PrimitiveTopology::Linelist: return vk::PrimitiveTopology::eLineList;
			case PrimitiveTopology::Linestrip: return vk::PrimitiveTopology::eLineStrip;
			case PrimitiveTopology::Trianglelist: return vk::PrimitiveTopology::eTriangleList;
			case PrimitiveTopology::Trianglestrip: return vk::PrimitiveTopology::eTriangleStrip;
			case PrimitiveTopology::Linelist_adj: return vk::PrimitiveTopology::eLineListWithAdjacency;
			case PrimitiveTopology::Linestrip_adj: return vk::PrimitiveTopology::eLineStripWithAdjacency;
			case PrimitiveTopology::Trianglelist_adj: return vk::PrimitiveTopology::eTriangleListWithAdjacency;
			case PrimitiveTopology::Trianglestrip_adj: return vk::PrimitiveTopology::eTriangleStripWithAdjacency;
			case PrimitiveTopology::Undefined: break;
			}
			seAssert(false, "missing primitive topology");
			return vk::PrimitiveTopology::eTriangleList;
		}

		inline vk::BlendFactor translate(Blend::Type blend_type)
		{
			switch (blend_type)
			{
			case Blend::Type::Zero: return vk::BlendFactor::eZero;
			case Blend::Type::One: return vk::BlendFactor::eOne;
			case Blend::Type::Src_color: return vk::BlendFactor::eSrcColor;
			case Blend::Type::Inv_src_color: return vk::BlendFactor::eOneMinusSrcColor;
			case Blend::Type::Src_alpha: return vk::BlendFactor::eSrcAlpha;
			case Blend::Type::Inv_src_alpha: return vk::BlendFactor::eOneMinusSrcAlpha;
			case Blend::Type::Dest_alpha: return vk::BlendFactor::eDstAlpha;
			case Blend::Type::Inv_dest_alpha: return vk::BlendFactor::eOneMinusDstAlpha;
			case Blend::Type::Dest_color: return vk::BlendFactor::eDstColor;
			case Blend::Type::Inv_dest_color: return vk::BlendFactor::eOneMinusDstColor;
			case Blend::Type::Src_alpha_sat: return vk::BlendFactor::eSrcAlphaSaturate;
			case Blend::Type::Blend_factor: return vk::BlendFactor::eConstantColor;
			case Blend::Type::Inv_blend_factor: return vk::BlendFactor::eOneMinusConstantColor;
			case Blend::Type::Src1_color: return vk::BlendFactor::eSrc1Color;
			case Blend::Type::Inv_src1_color: return vk::BlendFactor::eOneMinusSrc1Color;
			case Blend::Type::Src1_alpha: return vk::BlendFactor::eSrc1Alpha;
			case Blend::Type::Inv_src1_alpha: return vk::BlendFactor::eOneMinusSrc1Alpha;
			case Blend::Type::Alpha_factor: return vk::BlendFactor::eConstantAlpha;
			case Blend::Type::Inv_alpha_factor: return vk::BlendFactor::eOneMinusConstantAlpha;
			}
			seAssert(false, "missing blend factor");
			return vk::BlendFactor::eOne;
		}

		inline vk::BlendOp translate(Blend::Operation blend_operation)
		{
			switch (blend_operation)
			{
			case Blend::Operation::Add: return vk::BlendOp::eAdd;
			case Blend::Operation::Subtract: return vk::BlendOp::eSubtract;
			case Blend::Operation::ReverseSubtract: return vk::BlendOp::eReverseSubtract;
			case Blend::Operation::Min: return vk::BlendOp::eMin;
			case Blend::Operation::Max: return vk::BlendOp::eMax;
			}
			seAssert(false, "missing blend op");
			return vk::BlendOp::eAdd;
		}

		inline vk::LogicOp translate(Blend::LogicOperation logic_op)
		{
			switch (logic_op)
			{
			case Blend::LogicOperation::Clear: return vk::LogicOp::eClear;
			case Blend::LogicOperation::Set: return vk::LogicOp::eSet;
			case Blend::LogicOperation::Copy: return vk::LogicOp::eCopy;
			case Blend::LogicOperation::Copy_inverted: return vk::LogicOp::eCopyInverted;
			case Blend::LogicOperation::Noop: return vk::LogicOp::eNoOp;
			case Blend::LogicOperation::Invert: return vk::LogicOp::eInvert;
			case Blend::LogicOperation::And: return vk::LogicOp::eAnd;
			case Blend::LogicOperation::Nand: return vk::LogicOp::eNand;
			case Blend::LogicOperation::Or: return vk::LogicOp::eOr;
			case Blend::LogicOperation::Nor: return vk::LogicOp::eNor;
			case Blend::LogicOperation::Xor: return vk::LogicOp::eXor;
			case Blend::LogicOperation::Equiv: return vk::LogicOp::eEquivalent;
			case Blend::LogicOperation::And_reverse: return vk::LogicOp::eAndReverse;
			case Blend::LogicOperation::And_inverted: return vk::LogicOp::eAndInverted;
			case Blend::LogicOperation::Or_reverse: return vk::LogicOp::eOrReverse;
			case Blend::LogicOperation::Or_inverted: return vk::LogicOp::eOrInverted;
			}
			seAssert(false, "missing logic op");
			return vk::LogicOp::eNoOp;
		}

		inline vk::SamplerAddressMode translate(TextureAddressMode mode)
		{
			switch (mode)
			{
			case TextureAddressMode::Wrap: return vk::SamplerAddressMode::eRepeat;
			case TextureAddressMode::Mirror: return vk::SamplerAddressMode::eMirroredRepeat;
			case TextureAddressMode::Clamp: return vk::SamplerAddressMode::eClampToEdge;
			case TextureAddressMode::Border: return vk::SamplerAddressMode::eClampToBorder;
			case TextureAddressMode::MirrorOnce: return vk::SamplerAddressMode::eMirrorClampToEdge;
			}
			seAssert(false, "missing address mode");
			return vk::SamplerAddressMode::eRepeat;
		}

		// Vulkan only has fixed border colours (without VK_EXT_custom_border_color), so pick the closest one.
		inline vk::BorderColor translate_border_colour(const float colour[4])
		{
			if (colour[3] < 0.5f)
			{
				return vk::BorderColor::eFloatTransparentBlack;
			}
			const float luminance = (colour[0] + colour[1] + colour[2]) / 3.0f;
			return luminance < 0.5f ? vk::BorderColor::eFloatOpaqueBlack : vk::BorderColor::eFloatOpaqueWhite;
		}

		inline vk::IndexType translate_index_type(DXGI_FORMAT format)
		{
			seAssert(format == DXGI_FORMAT_R16_UINT || format == DXGI_FORMAT_R32_UINT, "Index buffers must be R16_UINT or R32_UINT");
			return format == DXGI_FORMAT_R16_UINT ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
		}
	}
}
