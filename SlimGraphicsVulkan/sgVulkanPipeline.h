#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"
#include <array>
#include <vector>

namespace sg
{
	namespace Vulkan
	{
		// Shader register classes, matching SlimShaderCompiler's -fvk-*-shift options (all in descriptor set 0):
		//   b<n> -> binding n, t<n> -> 16 + n, s<n> -> 32 + n, u<n> -> 48 + n
		namespace BindingShift
		{
			static constexpr u32 CBV = 0;
			static constexpr u32 SRV = 16;
			static constexpr u32 SAMPLER = 32;
			static constexpr u32 UAV = 48;
			static constexpr u32 END = 64;
		}

		// Push data written for every draw/dispatch. Every shader uses the same descriptor mapping (see
		// Device::create_descriptor_mapping), so pushed data stays valid across shader changes, like D3D12 root parameters.
		//   - CBVs (b0-b3) are read straight from device addresses (VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT)
		//   - SRVs / UAVs are tables of descriptors in the resource heap starting at the pushed slot index
		//     (VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT)
		//   - Samplers s<n> are the device's global samplers at sampler heap index n (constant offset)
		struct PushData
		{
			static constexpr u32 MAX_CBVS = 4;
			vk::DeviceAddress cbv_addresses[MAX_CBVS] = {};
			u32 srv_table_slot = 0;
			u32 uav_table_slot = 0;

			static constexpr u32 CBV_OFFSET = 0;
			static constexpr u32 SRV_TABLE_OFFSET = sizeof(vk::DeviceAddress) * MAX_CBVS;
			static constexpr u32 UAV_TABLE_OFFSET = SRV_TABLE_OFFSET + sizeof(u32);
		};
		static_assert(offsetof(PushData, srv_table_slot) == PushData::SRV_TABLE_OFFSET && offsetof(PushData, uav_table_slot) == PushData::UAV_TABLE_OFFSET, "Push data layout mismatch");
		static_assert(sizeof(PushData) % 4 == 0, "Push data size must be a multiple of 4");

		// Fixed function state of a graphics pipeline, all of it dynamic state with shader objects. Every colour attachment slot
		// is filled in (slots past the pipeline's render targets: blending off, all channels written) so it can be set as is.
		// CommandList skips setting it when consecutive pipelines share the same state.
		struct GraphicsState
		{
			static constexpr u32 MAX_RENDER_TARGETS = 8;

			vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
			vk::PolygonMode polygon_mode = vk::PolygonMode::eFill;
			vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eBack;
			vk::FrontFace front_face = vk::FrontFace::eClockwise;
			vk::Bool32 depth_test = VK_TRUE;
			vk::Bool32 depth_write = VK_TRUE;
			vk::CompareOp depth_compare = vk::CompareOp::eLessOrEqual;
			vk::Bool32 stencil_test = VK_FALSE;
			vk::StencilOpState stencil_front;
			vk::StencilOpState stencil_back;
			vk::Bool32 alpha_to_coverage = VK_FALSE;
			vk::Bool32 logic_op_enable = VK_FALSE;
			vk::LogicOp logic_op = vk::LogicOp::eNoOp;
			std::array<vk::Bool32, MAX_RENDER_TARGETS> blend_enables = {};
			std::array<vk::ColorBlendEquationEXT, MAX_RENDER_TARGETS> blend_equations = {};
			std::array<vk::ColorComponentFlags, MAX_RENDER_TARGETS> write_masks = {};

			bool operator==(const GraphicsState&) const = default;
		};

		// A set of (linked) shader objects plus the fixed function state they are drawn with. CommandList::set_pipeline binds
		// the shaders and sets the state.
		class Pipeline
		{
			friend class Device;
			friend class CommandList;
		public:
			Pipeline(const BindingDesc& pipeline_binding_desc, vk::PipelineBindPoint bind_point_) : binding_desc(pipeline_binding_desc), bind_point(bind_point_) {}

			const BindingDesc& get_binding_desc() const { return binding_desc; }
			vk::PipelineBindPoint get_bind_point() const { return bind_point; }

		private:
			// An SRV or UAV table entry the shaders read (unset entries get a stand-in descriptor of this type)
			struct TableEntry
			{
				u32 binding = 0;
				vk::DescriptorType type = vk::DescriptorType::eSampledImage;
			};

			struct VertexAttribute
			{
				u32 location = 0;
				vk::Format format = vk::Format::eUndefined;
				u32 offset = 0;
				vk::VertexInputRate rate = vk::VertexInputRate::eVertex;
			};

			static constexpr u32 MAX_BOUND_STAGES = 5; // Vertex, fragment, geometry, task, mesh

			const BindingDesc binding_desc;
			const vk::PipelineBindPoint bind_point;

			std::vector<vk::raii::ShaderEXT> shaders;
			// What vkCmdBindShadersEXT is called with: every stage of the bind point the device supports, VK_NULL_HANDLE for
			// the stages this pipeline does not use
			std::array<vk::ShaderStageFlagBits, MAX_BOUND_STAGES> bound_stages = {};
			std::array<vk::ShaderEXT, MAX_BOUND_STAGES> bound_shaders = {};
			u32 bound_stage_count = 0;
			bool has_vertex_shader = false;

			std::vector<TableEntry> table_entries;
			u32 srv_table_size = 0;
			u32 uav_table_size = 0;
			u32 used_cbv_mask = 0; // Bit n set: the shaders read b<n>

			std::vector<VertexAttribute> vertex_attributes; // All in vertex buffer slot 0
			GraphicsState graphics_state;
		};
	}
}
