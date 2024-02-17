#pragma once
#include <sgTypes.h>
#include "sgD3D12Include.h"

namespace sg
{
	namespace D3D12
	{
		D3D12_CULL_MODE translate(Rasterizer::CullMode cull_mode)
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

		D3D12_FILL_MODE translate(Rasterizer::FillMode fill_mode)
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

		D3D12_COMPARISON_FUNC translate(ComparisonFunction comp_func)
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

		D3D12_STENCIL_OP translate(DepthStencil::StencilOperation stencil_op)
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

		D3D12_PRIMITIVE_TOPOLOGY_TYPE translate(Topology topology)
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

		D3D12_DEPTH_STENCILOP_DESC translate(const DepthStencil::StencilDesc& stencil_desc)
		{
			D3D12_DEPTH_STENCILOP_DESC sd = {};
			sd.StencilFunc = translate(stencil_desc.comparison_operation);
			sd.StencilFailOp = translate(stencil_desc.fail_operation);
			sd.StencilDepthFailOp = translate(stencil_desc.depth_operation);
			sd.StencilPassOp = translate(stencil_desc.pass_operation);
			return sd;
		}

		D3D12_RASTERIZER_DESC translate(const Rasterizer::Desc& desc)
		{
			CD3DX12_RASTERIZER_DESC rd(D3D12_DEFAULT);	
			rd.CullMode = translate(desc.cull_mode);
			rd.FillMode = translate(desc.fill_mode);
			rd.FrontCounterClockwise = desc.front_counter_clockwise;
			return rd;
		}

		D3D12_DEPTH_STENCIL_DESC translate(const DepthStencil::Desc& desc)
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

		D3D12_BLEND translate(Blend::Type blend_type)
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

		D3D12_BLEND_OP translate(const Blend::Operation blend_operation)
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

		D3D12_LOGIC_OP translate(Blend::LogicOperation logic_op)
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

		D3D12_RENDER_TARGET_BLEND_DESC translate(const Blend::RenderTargetDesc& desc)
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

		D3D12_BLEND_DESC translate(const Blend::Desc& desc)
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
	}
}