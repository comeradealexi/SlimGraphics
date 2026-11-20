#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"
#include <string>
#include <vector>

namespace sg
{
	namespace Vulkan
	{
		// Minimal SPIR-V reflection: just what is needed to build pipeline layouts and vertex input state from DXC output.
		struct SpirvReflection
		{
			struct DescriptorBinding
			{
				u32 set = 0;
				u32 binding = 0;
				u32 count = 1;
				vk::DescriptorType type = vk::DescriptorType::eUniformBuffer;
			};

			struct VertexInput
			{
				u32 location = 0;
				std::string semantic; // From DXC's "in.var.<SEMANTIC>" variable names, e.g. "POSITION", "TEXCOORD0"
			};

			std::string entry_point;
			vk::ShaderStageFlagBits stage = vk::ShaderStageFlagBits::eVertex;
			std::vector<DescriptorBinding> bindings;
			std::vector<VertexInput> vertex_inputs;
		};

		// Returns false if the module is not valid SPIR-V or has no entry point
		bool reflect_spirv(const u32* code, size_t word_count, SpirvReflection& out_reflection);
	}
}
