#pragma once
#include <stdint.h>
#include <memory>

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

	namespace Pipeline
	{
		struct ComputeDesc
		{

		};

		struct GraphicsDesc
		{

		};
	}
}