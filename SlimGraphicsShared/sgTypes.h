#pragma once
#include "sgPlatformForwardDeclare.h"
#include <stdint.h>
#include <memory>
#include <dxgiformat.h>

//sg = simple graphics
namespace sg
{
	template <typename T>
	using Ptr = std::unique_ptr<T>;

	template <typename T>
	using SharedPtr = std::shared_ptr<T>;

	using u8 = uint8_t;
	using u16 = uint16_t;
	using u32 = uint32_t;
	using u64 = uint64_t;

	using i8 = int8_t;
	using i16 = int16_t;
	using i32 = int32_t;
	using i64 = int64_t;

	enum class MemoryType
	{
		GPUOptimal, Upload, Readback
	};

	enum class MemorySubType
	{
		None, Texture, Target, Buffer
	};

	enum class Topology
	{
		Undefined, Point, Line,	Triangle, Patch
	};

	enum class PrimitiveTopology
	{
		Undefined,
		Pointlist,
		Linelist,
		Linestrip,
		Trianglelist,
		Trianglestrip,
		Linelist_adj,
		Linestrip_adj,
		Trianglelist_adj,
		Trianglestrip_adj,
	};

	enum class PipelineType
	{
		Geometry, Compute
	};

	enum class ShaderType
	{
		Compute, Pixel, Vertex
	};

	namespace Rasterizer
	{
		enum class FillMode
		{
			Wireframe, Solid
		};
		enum class CullMode
		{
			None, Front, Back
		};
		struct Desc
		{
			FillMode fill_mode = FillMode::Solid;
			CullMode cull_mode = CullMode::Back;
			bool front_counter_clockwise = false;
		};
	}

	namespace Blend
	{
		enum class Type
		{
			Zero = 1,
			One = 2,
			Src_color = 3,
			Inv_src_color = 4,
			Src_alpha = 5,
			Inv_src_alpha = 6,
			Dest_alpha = 7,
			Inv_dest_alpha = 8,
			Dest_color = 9,
			Inv_dest_color = 10,
			Src_alpha_sat = 11,
			Blend_factor = 14,
			Inv_blend_factor = 15,
			Src1_color = 16,
			Inv_src1_color = 17,
			Src1_alpha = 18,
			Inv_src1_alpha = 19,
			Alpha_factor = 20,
			Inv_alpha_factor = 21
		};

		enum class Operation
		{
			Add, Subtract, ReverseSubtract, Min, Max
		};

		enum class LogicOperation
		{
			Clear = 0,
			Set,
			Copy,
			Copy_inverted,
			Noop,
			Invert,
			And,
			Nand,
			Or,
			Nor,
			Xor,
			Equiv,
			And_reverse,
			And_inverted,
			Or_reverse,
			Or_inverted
		};
		
		struct RenderTargetDesc
		{
			bool blend_enable = false;
			bool logic_op_enable = false;
			Type src_blend = Type::One;
			Type dest_blend = Type::Zero;
			Operation blend_op = Operation::Add;
			Type src_blend_alpha = Type::One;
			Type dest_blend_alpha = Type::Zero;
			Operation blend_op_alpha = Operation::Add;
			LogicOperation logic_op = LogicOperation::Noop;
			uint8_t render_target_write_mask = 0xf;
		};

		struct Desc
		{
			bool alpha_to_coverage_enable = false;
			bool independent_blend_enable = false;
			RenderTargetDesc render_targets[8];
		};
	}

	enum class ComparisonFunction
	{
		None, Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always
	};

	namespace DepthStencil
	{
		enum class StencilOperation
		{
			Keep, Zero, Replace, IncreaseClamp, DecreaseClamp, Invert, IncreaseWrap, DecreaseWrap
		};

		struct StencilDesc
		{
			StencilOperation fail_operation = StencilOperation::Keep;
			StencilOperation depth_operation = StencilOperation::Keep;
			StencilOperation pass_operation = StencilOperation::Keep;
			ComparisonFunction comparison_operation = ComparisonFunction::Always;
		};
		struct Desc
		{
			bool depth_enable = true;
			bool depth_write = true;
			ComparisonFunction comparison_function = ComparisonFunction::LessEqual;
			bool stencil_enable = false;
			uint8_t stencil_read_mask = 0xff;
			uint8_t stencil_write_mask = 0xff;
			StencilDesc stencil_front_desc;
			StencilDesc stencil_back_desc;
		};
	}

	namespace InputLayout
	{
		enum class InputClassification
		{
			PerVertex, PerInstance
		};

		struct ElementDesc
		{
			char semantic_name[32] = {};
			u32 semantic_index = 0;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
			u32 input_slot = 0;
			u32 aligned_byte_offset = 0;
			InputClassification input_classification = InputClassification::PerVertex;
			u32 instance_data_step_rate = 0;
		};

		struct Desc
		{
			static constexpr size_t MAX_ELEMENTS = 8;
			ElementDesc elements[MAX_ELEMENTS];
			u32 num_elements = 0;
		};
	}

	namespace PipelineDesc
	{
		struct Compute
		{
			ComputeShader* compute_shader = nullptr;
		};

		struct Graphics
		{
			VertexShader* vertex_shader = nullptr;
			PixelShader* pixel_shader = nullptr;

			Topology topology = Topology::Triangle;
			PrimitiveTopology primitive_topology = PrimitiveTopology::Trianglelist;

			Rasterizer::Desc rasterizer_desc;
			DepthStencil::Desc depth_stencil_desc;
			InputLayout::Desc input_layout;
			Blend::Desc blend_desc;

			DXGI_FORMAT render_target_format_list[8] = {};
			u32 render_target_count = 0;

			DXGI_FORMAT depth_stencil_format = {};
		};
	}

	struct BindingDesc
	{
		static constexpr size_t MAX_CBVS = 4;
		static constexpr size_t MAX_SRVS = 16;
		static constexpr size_t MAX_UAVS = 16;
		static constexpr size_t MAX_SAMPLERS = 8;

		//enum class Type { ConstantBufferView, ShaderResourceView, UnorderedAccessView, Sampler };
		u32 cbv_binding_count = 0;
		u32 srv_binding_count = 0;
		u32 uav_binding_count = 0;
		u32 sampler_binding_count = 0;
	};

	using CBVBinding = u32;
	using SRVBinding = u32;
	using UAVBinding = u32;
	using SamplerBinding = u32;

	using RTVBinding = u32;
	using DSVBinding = u32;

	struct Binding : public BindingDesc
	{
		void set_cbv(CBVBinding binding, u32 index)
		{
			dirty = true;
			cbvs[index] = binding;
		}
		void set_srv(SRVBinding binding, u32 index)
		{
			dirty = true;
			srvs[index] = binding;
		}
		void set_uav(UAVBinding binding, u32 index)
		{
			dirty = true;
			uavs[index] = binding;
		}
		void set_sampler(SamplerBinding binding, u32 index)
		{
			dirty = true;
			samplers[index] = binding;
		}
		void set_not_dirty() { dirty = false; }

		const CBVBinding get_cbvs(u32 idx) const { return cbvs[idx]; }
		const SRVBinding get_srvs(u32 idx) const { return srvs[idx]; }
		const UAVBinding get_uavs(u32 idx) const { return uavs[idx]; }
		const SamplerBinding get_samplers(u32 idx) const { return samplers[idx]; }

	private:
		bool dirty = true;
		CBVBinding cbvs[MAX_CBVS] = {};
		SRVBinding srvs[MAX_SRVS] = {};
		UAVBinding uavs[MAX_UAVS] = {};
		SamplerBinding samplers[MAX_SAMPLERS] = {};
	};
}