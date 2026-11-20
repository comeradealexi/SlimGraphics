#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"
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

		// A set of (linked) shader objects plus the fixed function state they are drawn with. All of it is dynamic state with
		// VK_EXT_shader_object; CommandList::set_pipeline binds the shaders and sets the state.
		class Pipeline
		{
			friend class Device;
			friend class CommandList;
		public:
			Pipeline(const BindingDesc& pipeline_binding_desc, vk::PipelineBindPoint bind_point_) : binding_desc(pipeline_binding_desc), bind_point(bind_point_) {}

			const BindingDesc& get_binding_desc() const { return binding_desc; }
			vk::PipelineBindPoint get_bind_point() const { return bind_point; }

		private:
			struct DescriptorUse
			{
				u32 binding = 0;
				vk::DescriptorType type = vk::DescriptorType::eUniformBuffer;
			};

			struct VertexAttribute
			{
				u32 location = 0;
				u32 slot = 0;
				vk::Format format = vk::Format::eUndefined;
				u32 offset = 0;
				vk::VertexInputRate rate = vk::VertexInputRate::eVertex;
			};

			const BindingDesc binding_desc;
			const vk::PipelineBindPoint bind_point;

			std::vector<vk::ShaderStageFlagBits> stages;
			std::vector<vk::raii::ShaderEXT> shaders;
			bool has_vertex_shader = false;

			// Descriptors the shaders use (to fill unset table entries with valid descriptors)
			std::vector<DescriptorUse> descriptor_uses;
			u32 srv_table_size = 0;
			u32 uav_table_size = 0;

			// Graphics state
			std::vector<VertexAttribute> vertex_attributes;
			vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
			vk::PolygonMode polygon_mode = vk::PolygonMode::eFill;
			vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eBack;
			vk::FrontFace front_face = vk::FrontFace::eClockwise;
			bool depth_test = true;
			bool depth_write = true;
			vk::CompareOp depth_compare = vk::CompareOp::eLessOrEqual;
			bool stencil_test = false;
			vk::StencilOpState stencil_front;
			vk::StencilOpState stencil_back;
			bool alpha_to_coverage = false;
			bool logic_op_enable = false;
			vk::LogicOp logic_op = vk::LogicOp::eNoOp;
			u32 render_target_count = 0;
			std::vector<vk::Bool32> blend_enables;
			std::vector<vk::ColorBlendEquationEXT> blend_equations;
			std::vector<vk::ColorComponentFlags> write_masks;
		};
	}
}
