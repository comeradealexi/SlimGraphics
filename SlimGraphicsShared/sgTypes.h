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

	enum class MemoryType
	{
		GPUOptimal, Upload, Readback
	};

	enum class Topology
	{
		Undefined, Point, Line,	Triangle, Patch
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
			ComparisonFunction comparison_operation = ComparisonFunction::None;
		};
		struct Desc
		{
			bool depth_enable = true;
			bool depth_write = true;
			ComparisonFunction comparison_function = ComparisonFunction::LessEqual;
			bool stencil_enable = false;
			uint8_t stencil_read_mask = 0;
			uint8_t stencil_write_mask = 0;
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

	namespace Pipeline
	{
		struct ComputeDesc
		{
			ComputeShader* compute_shader = nullptr;
		};

		struct GraphicsDesc
		{
			VertexShader* vertex_shader = nullptr;
			PixelShader* pixel_shader = nullptr;

			Rasterizer::Desc rasterizer_desc;
			DepthStencil::Desc depth_stencil_desc;
			InputLayout::Desc input_layout;

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

	struct Binding : public BindingDesc
	{
		ConstantBufferView* cbvs[MAX_CBVS] = {};
		ShaderResourceView* srvs[MAX_SRVS] = {};
		UnorderedAccessView* uavs[MAX_UAVS] = {};
		Sampler* samplers[MAX_SAMPLERS] = {};
	};
}