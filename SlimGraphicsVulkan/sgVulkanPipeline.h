#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"

namespace sg
{
	namespace Vulkan
	{
		// Register classes, matching SlimShaderCompiler's -fvk-*-shift options (all in descriptor set 0):
		//   b<n> -> binding n, t<n> -> 16 + n, s<n> -> 32 + n, u<n> -> 48 + n
		namespace BindingShift
		{
			static constexpr u32 CBV = 0;
			static constexpr u32 SRV = 16;
			static constexpr u32 SAMPLER = 32;
			static constexpr u32 UAV = 48;
			static constexpr u32 END = 64;
		}

		// A push descriptor set layout and the pipeline layout built from it. Shared between pipelines whose shaders
		// use exactly the same bindings, which also keeps pushed descriptors compatible when switching between them.
		struct PipelineLayout
		{
			struct Binding
			{
				u32 binding = 0;
				vk::DescriptorType type = vk::DescriptorType::eUniformBuffer;
				vk::ShaderStageFlags stages;
			};

			std::vector<Binding> bindings; // Sorted by binding number
			vk::raii::DescriptorSetLayout set_layout = nullptr;
			vk::raii::PipelineLayout layout = nullptr;
		};

		class Pipeline
		{
			friend class Device;
			friend class CommandList;
		public:
			Pipeline(const BindingDesc& pipeline_binding_desc, vk::PipelineBindPoint bind_point_) : binding_desc(pipeline_binding_desc), bind_point(bind_point_) {}

			const BindingDesc& get_binding_desc() const { return binding_desc; }
			vk::PipelineBindPoint get_bind_point() const { return bind_point; }

		private:
			const BindingDesc binding_desc;
			const vk::PipelineBindPoint bind_point;
			SharedPtr<PipelineLayout> layout;
			vk::raii::Pipeline pipeline = nullptr;
		};
	}
}
