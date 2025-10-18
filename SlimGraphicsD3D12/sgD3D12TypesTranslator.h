#pragma once
#include <sgTypes.h>
#include "sgD3D12Include.h"

namespace sg
{
	namespace D3D12
	{
		static D3D12_VIEWPORT translate(const Viewport& viewport)
		{
			return CD3DX12_VIEWPORT(viewport.top_left_x, viewport.top_left_y, viewport.width, viewport.height, viewport.min_depth, viewport.max_depth);
		}

		static D3D12_RECT translate(const ScissorRect& scissor)
		{
			return CD3DX12_RECT(scissor.left, scissor.top, scissor.right, scissor.bottom);
		}
		static D3D12_RESOURCE_FLAGS translate(ResourceUsageFlags flags)
		{
			D3D12_RESOURCE_FLAGS out_flags = D3D12_RESOURCE_FLAG_NONE;
			
			if ((flags & ResourceUsageFlags::RenderTarget) == ResourceUsageFlags::RenderTarget)
			{
				out_flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			}

			if ((flags & ResourceUsageFlags::DepthStencil) == ResourceUsageFlags::DepthStencil)
			{
				out_flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			}

			if ((flags & ResourceUsageFlags::UnorderedAccess) == ResourceUsageFlags::UnorderedAccess)
			{
				out_flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			}

			return out_flags;
		}

		static D3D12_RESOURCE_DIMENSION translate(ResourceDimension rd)
		{
			switch (rd)
			{
			case ResourceDimension::Buffer:
				return D3D12_RESOURCE_DIMENSION_BUFFER;
			case ResourceDimension::Texture1D:
				return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
			case ResourceDimension::Texture2D:
				return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			case ResourceDimension::Texture3D:
				return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
			default:
				seAssert(false, "missing resource dim");
				return D3D12_RESOURCE_DIMENSION_UNKNOWN;
			}
		}

		static D3D12_SRV_DIMENSION translate_srv(ResourceDimension rd)
		{
			switch (rd)
			{
			case ResourceDimension::Buffer:
				return D3D12_SRV_DIMENSION_BUFFER;
			case ResourceDimension::Texture1D:
				return D3D12_SRV_DIMENSION_TEXTURE1D;
			case ResourceDimension::Texture2D:
				return D3D12_SRV_DIMENSION_TEXTURE2D;
			case ResourceDimension::Texture3D:
				return D3D12_SRV_DIMENSION_TEXTURE3D;
			default:
				seAssert(false, "missing resource dim");
				return D3D12_SRV_DIMENSION_UNKNOWN;
			}
		}

		static D3D12_RESOURCE_STATES get_d3d12_resource_read_state(BufferType type, bool is_readback = false)
		{
			D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
			switch (type)
			{
			case BufferType::Vertex:
			case BufferType::Constant:
				state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
				break;
			case BufferType::Index:
				state = D3D12_RESOURCE_STATE_INDEX_BUFFER;
				break;
			case BufferType::GeneralDataBuffer:
				if (is_readback) state = D3D12_RESOURCE_STATE_COPY_DEST;
				else state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
				break;
			case BufferType::Upload:
				state = D3D12_RESOURCE_STATE_GENERIC_READ;
				break;
			case BufferType::Texture:
				state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
				break;
			default:
				seAssert(false, "Missing case statement");
				break;
			}
			return state;
		}

		static D3D12_CULL_MODE translate(Rasterizer::CullMode cull_mode)
		{
			switch (cull_mode)
			{
			case Rasterizer::CullMode::None:
				return D3D12_CULL_MODE_NONE;
			case Rasterizer::CullMode::Front:
				return D3D12_CULL_MODE_FRONT;
			case Rasterizer::CullMode::Back:
				return D3D12_CULL_MODE_BACK;
			}
			seAssert(false, "missing cull mode");
			return D3D12_CULL_MODE_BACK;
		}

		static D3D12_FILL_MODE translate(Rasterizer::FillMode fill_mode)
		{
			switch (fill_mode)
			{
			case Rasterizer::FillMode::Wireframe:
				return D3D12_FILL_MODE_WIREFRAME;
			case Rasterizer::FillMode::Solid:
				return D3D12_FILL_MODE_SOLID;
			}
			seAssert(false, "missing fill mode");
			return D3D12_FILL_MODE_SOLID;
		}

		static D3D12_COMPARISON_FUNC translate(ComparisonFunction comp_func)
		{
			switch (comp_func)
			{
			case ComparisonFunction::None:
				return D3D12_COMPARISON_FUNC_NONE;
			case ComparisonFunction::Never:
				return D3D12_COMPARISON_FUNC_NEVER;
			case ComparisonFunction::Less:
				return D3D12_COMPARISON_FUNC_LESS;
			case ComparisonFunction::Equal:
				return D3D12_COMPARISON_FUNC_EQUAL;
			case ComparisonFunction::LessEqual:
				return D3D12_COMPARISON_FUNC_LESS_EQUAL;
			case ComparisonFunction::Greater:
				return D3D12_COMPARISON_FUNC_GREATER;
			case ComparisonFunction::NotEqual:
				return D3D12_COMPARISON_FUNC_NOT_EQUAL;
			case ComparisonFunction::GreaterEqual:
				return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
			case ComparisonFunction::Always:
				return D3D12_COMPARISON_FUNC_ALWAYS;
			}
			seAssert(false, "missing depth func mode");
			return D3D12_COMPARISON_FUNC_ALWAYS;
		}

		static D3D12_STENCIL_OP translate(DepthStencil::StencilOperation stencil_op)
		{
			switch (stencil_op)
			{
			case DepthStencil::StencilOperation::Keep:
				return D3D12_STENCIL_OP_KEEP;
			case DepthStencil::StencilOperation::Zero:
				return D3D12_STENCIL_OP_ZERO;
			case DepthStencil::StencilOperation::Replace:
				return D3D12_STENCIL_OP_REPLACE;
			case DepthStencil::StencilOperation::IncreaseClamp:
				return D3D12_STENCIL_OP_INCR_SAT;
			case DepthStencil::StencilOperation::DecreaseClamp:
				return D3D12_STENCIL_OP_DECR_SAT;
			case DepthStencil::StencilOperation::Invert:
				return D3D12_STENCIL_OP_INVERT;
			case DepthStencil::StencilOperation::IncreaseWrap:
				return D3D12_STENCIL_OP_INCR;
			case DepthStencil::StencilOperation::DecreaseWrap:
				return D3D12_STENCIL_OP_DECR;
			}
			seAssert(false, "missing StencilOperation mode");
			return D3D12_STENCIL_OP_KEEP;
		}

		static D3D12_PRIMITIVE_TOPOLOGY_TYPE translate(Topology topology)
		{
			switch (topology)
			{
				case Topology::Undefined:
					return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
				case Topology::Point:
					return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
				case Topology::Line:
					return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
				case Topology::Triangle:
					return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
				case Topology::Patch:
					return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

			}
			seAssert(false, "missing Topology");
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
		}

		static D3D_PRIMITIVE_TOPOLOGY translate(PrimitiveTopology toplogy)
		{
			switch (toplogy)
			{
				case PrimitiveTopology::Undefined:
					return         D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
				case PrimitiveTopology::Pointlist:
					return         D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
				case PrimitiveTopology::Linelist:
					return         D3D_PRIMITIVE_TOPOLOGY_LINELIST;
				case PrimitiveTopology::Linestrip:
					return         D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
				case PrimitiveTopology::Trianglelist:
					return         D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
				case PrimitiveTopology::Trianglestrip:
					return         D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
				case PrimitiveTopology::Linelist_adj:
					return         D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ ;
				case PrimitiveTopology::Linestrip_adj:
					return         D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ ;
				case PrimitiveTopology::Trianglelist_adj:
					return         D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ ;
				case PrimitiveTopology::Trianglestrip_adj:
					return         D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ ;
			}
			seAssert(false, "missing Topology");
			return         D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		}

		static D3D12_DEPTH_STENCILOP_DESC translate(const DepthStencil::StencilDesc& stencil_desc)
		{
			D3D12_DEPTH_STENCILOP_DESC sd = {};
			sd.StencilFunc = translate(stencil_desc.comparison_operation);
			sd.StencilFailOp = translate(stencil_desc.fail_operation);
			sd.StencilDepthFailOp = translate(stencil_desc.depth_operation);
			sd.StencilPassOp = translate(stencil_desc.pass_operation);
			return sd;
		}

		static D3D12_RASTERIZER_DESC translate(const Rasterizer::Desc& desc)
		{
			CD3DX12_RASTERIZER_DESC rd(D3D12_DEFAULT);	
			rd.CullMode = translate(desc.cull_mode);
			rd.FillMode = translate(desc.fill_mode);
			rd.FrontCounterClockwise = desc.front_counter_clockwise;
			return rd;
		}

		static D3D12_DEPTH_STENCIL_DESC translate(const DepthStencil::Desc& desc)
		{
			CD3DX12_DEPTH_STENCIL_DESC ds(D3D12_DEFAULT);

			ds.DepthFunc = translate(desc.comparison_function);
			ds.DepthEnable = desc.depth_enable;
			ds.DepthWriteMask = desc.depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
			ds.StencilEnable = desc.stencil_enable;
			ds.StencilReadMask = desc.stencil_read_mask;
			ds.StencilWriteMask = desc.stencil_write_mask;
			ds.FrontFace = translate(desc.stencil_front_desc);
			ds.BackFace = translate(desc.stencil_back_desc);
			return ds;
		}

		static D3D12_BLEND translate(Blend::Type blend_type)
		{
			switch (blend_type)
			{
			case Blend::Type::Zero:
				return D3D12_BLEND_ZERO;
			case Blend::Type::One:
				return D3D12_BLEND_ONE;
			case Blend::Type::Src_color:
				return D3D12_BLEND_SRC_COLOR;
			case Blend::Type::Inv_src_color:
				return D3D12_BLEND_INV_SRC_COLOR;
			case Blend::Type::Src_alpha:
				return D3D12_BLEND_SRC_ALPHA;
			case Blend::Type::Inv_src_alpha:
				return D3D12_BLEND_INV_SRC_ALPHA;
			case Blend::Type::Dest_alpha:
				return D3D12_BLEND_DEST_ALPHA;
			case Blend::Type::Inv_dest_alpha:
				return D3D12_BLEND_INV_DEST_ALPHA;
			case Blend::Type::Dest_color:
				return D3D12_BLEND_DEST_COLOR;
			case Blend::Type::Inv_dest_color:
				return D3D12_BLEND_INV_DEST_COLOR;
			case Blend::Type::Src_alpha_sat:
				return D3D12_BLEND_SRC_ALPHA_SAT;
			case Blend::Type::Blend_factor:
				return D3D12_BLEND_BLEND_FACTOR;
			case Blend::Type::Inv_blend_factor:
				return D3D12_BLEND_INV_BLEND_FACTOR;
			case Blend::Type::Src1_color:
				return D3D12_BLEND_SRC1_COLOR;
			case Blend::Type::Inv_src1_color:
				return D3D12_BLEND_INV_SRC1_COLOR;
			case Blend::Type::Src1_alpha:
				return D3D12_BLEND_SRC1_ALPHA;
			case Blend::Type::Inv_src1_alpha:
				return D3D12_BLEND_INV_SRC1_ALPHA;
			case Blend::Type::Alpha_factor:
				return D3D12_BLEND_ALPHA_FACTOR;
			case Blend::Type::Inv_alpha_factor:
				return D3D12_BLEND_INV_ALPHA_FACTOR;
			}
			seAssert(false, "missing blend mode");
			return D3D12_BLEND_ONE;
		}

		static D3D12_BLEND_OP translate(const Blend::Operation blend_operation)
		{
			switch (blend_operation)
			{
				case Blend::Operation::Add:
					return D3D12_BLEND_OP_ADD;
				case Blend::Operation::Subtract:
					D3D12_BLEND_OP_SUBTRACT;
				case Blend::Operation::ReverseSubtract:
					D3D12_BLEND_OP_REV_SUBTRACT;
				case Blend::Operation::Min:
					return D3D12_BLEND_OP_MIN;
				case Blend::Operation::Max:
					return D3D12_BLEND_OP_MAX;

			}
			seAssert(false, "missing blend op");
			return D3D12_BLEND_OP_ADD;
		}

		static D3D12_LOGIC_OP translate(Blend::LogicOperation logic_op)
		{
			switch (logic_op)
			{
			case Blend::LogicOperation::Clear:
				return D3D12_LOGIC_OP_CLEAR;
			case Blend::LogicOperation::Set:
				return D3D12_LOGIC_OP_SET;
			case Blend::LogicOperation::Copy:
				return D3D12_LOGIC_OP_COPY;
			case Blend::LogicOperation::Copy_inverted:
				return D3D12_LOGIC_OP_COPY_INVERTED;
			case Blend::LogicOperation::Noop:
				return D3D12_LOGIC_OP_NOOP;
			case Blend::LogicOperation::Invert:
				return D3D12_LOGIC_OP_INVERT;
			case Blend::LogicOperation::And:
				return D3D12_LOGIC_OP_AND;
			case Blend::LogicOperation::Nand:
				return D3D12_LOGIC_OP_NAND;
			case Blend::LogicOperation::Or:
				return D3D12_LOGIC_OP_OR;
			case Blend::LogicOperation::Nor:
				return D3D12_LOGIC_OP_NOR;
			case Blend::LogicOperation::Xor:
				return D3D12_LOGIC_OP_XOR;
			case Blend::LogicOperation::Equiv:
				return D3D12_LOGIC_OP_EQUIV;
			case Blend::LogicOperation::And_reverse:
				return D3D12_LOGIC_OP_AND_REVERSE;
			case Blend::LogicOperation::And_inverted:
				return D3D12_LOGIC_OP_AND_INVERTED;
			case Blend::LogicOperation::Or_reverse:
				return D3D12_LOGIC_OP_OR_REVERSE;
			case Blend::LogicOperation::Or_inverted:
				return D3D12_LOGIC_OP_OR_INVERTED;
			}
			seAssert(false, "missing logic op");
			return D3D12_LOGIC_OP_NOOP;
		}

		static D3D12_RENDER_TARGET_BLEND_DESC translate(const Blend::RenderTargetDesc& desc)
		{
			D3D12_RENDER_TARGET_BLEND_DESC  rtd = {};
			rtd.BlendEnable = desc.blend_enable;
			rtd.LogicOpEnable = desc.logic_op_enable;
			rtd.SrcBlend = translate(desc.src_blend);
			rtd.DestBlend = translate(desc.dest_blend);
			rtd.BlendOp = translate(desc.blend_op);
			rtd.SrcBlendAlpha = translate(desc.src_blend_alpha);
			rtd.DestBlendAlpha = translate(desc.dest_blend_alpha);
			rtd.BlendOpAlpha = translate(desc.blend_op_alpha);
			rtd.LogicOp = translate(desc.logic_op);
			rtd.RenderTargetWriteMask = desc.render_target_write_mask;
			return rtd;
		}

		static D3D12_BLEND_DESC translate(const Blend::Desc& desc)
		{
			CD3DX12_BLEND_DESC bd(D3D12_DEFAULT);

			bd.AlphaToCoverageEnable = desc.alpha_to_coverage_enable;
			bd.IndependentBlendEnable = desc.independent_blend_enable;
			for (size_t i = 0; i < 8; i++)
			{
				bd.RenderTarget[i] = translate(desc.render_targets[i]);
			}			
			return bd;
		}

		static D3D12_INPUT_CLASSIFICATION translate(const sg::InputLayout::InputClassification& input_class)
		{
			switch (input_class)
			{
			case sg::InputLayout::InputClassification::PerVertex:
				return D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			case sg::InputLayout::InputClassification::PerInstance:
				return D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
			default:
				seAssert(false, "missing input class");
				return D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			}
		}

		static D3D12_FILTER translate(sg::Filter filter)
		{
			switch (filter)
			{
			case sg::Filter::MinMagMipPoint:
				return D3D12_FILTER_MIN_MAG_MIP_POINT;
			case sg::Filter::MinMagPointMipLinear:
				return D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
			case sg::Filter::MinPointMagLinearMipPoint:
				return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
			case sg::Filter::MinPointMagMipLinear:
				return D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
			case sg::Filter::MinLinearMagMipPoint:
				return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
			case sg::Filter::MinLinearMagPointMipLinear:
				return D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
			case sg::Filter::MinMagLinearMipPoint:
				return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			case sg::Filter::MinMagMipLinear:
				return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			case sg::Filter::MinMagAnisotropicMipPoint:
				return D3D12_FILTER_MIN_MAG_ANISOTROPIC_MIP_POINT;
			case sg::Filter::Anisotropic:
				return D3D12_FILTER_ANISOTROPIC;
			default:
				seAssert(false, "Missing filter translation");
				return {};
			}
		}

		static D3D12_TEXTURE_ADDRESS_MODE translate(sg::TextureAddressMode texture_address)
		{
			switch (texture_address)
			{
			case sg::TextureAddressMode::Wrap:
				return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			case sg::TextureAddressMode::Mirror:
				return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
			case sg::TextureAddressMode::Clamp:
				return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			case sg::TextureAddressMode::Border:
				return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			case sg::TextureAddressMode::MirrorOnce:
				return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
			default:
				seAssert(false, "Missing filter translation");
				return {};
			}
		}

		static D3D12_SAMPLER_DESC translate(const sg::SamplerDesc& sampler_desc)
		{
			D3D12_SAMPLER_DESC sd = {};
			sd.Filter = translate(sampler_desc.filter);
			sd.AddressU = translate(sampler_desc.address_u);
			sd.AddressV = translate(sampler_desc.address_v);
			sd.AddressW = translate(sampler_desc.address_w);
			sd.MipLODBias = sampler_desc.mip_lod_bias;
			sd.MaxAnisotropy = sampler_desc.max_anisotropy;
			sd.ComparisonFunc = translate(sampler_desc.comparison_func);
			sd.BorderColor[0] = sampler_desc.border_color[0];
			sd.BorderColor[1] = sampler_desc.border_color[1];
			sd.BorderColor[2] = sampler_desc.border_color[2];
			sd.BorderColor[3] = sampler_desc.border_color[3];
			sd.MinLOD = sampler_desc.min_lod;
			sd.MaxLOD = sampler_desc.max_lod;

			return sd;
		}
	}
}