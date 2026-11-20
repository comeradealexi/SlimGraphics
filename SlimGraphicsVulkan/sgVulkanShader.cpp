#include "pch.h"
#include "sgVulkanShader.h"

namespace sg
{
	namespace Vulkan
	{
		Shader::Shader(const vk::raii::Device& device, const std::vector<uint8_t>& spirv)
		{
			seAssert(spirv.size() % 4 == 0 && !spirv.empty(), "SPIR-V size must be a non-zero multiple of 4 bytes");
			// Copy into word storage so the code is correctly aligned for parsing and module creation
			std::vector<u32> words(spirv.size() / 4);
			memcpy(words.data(), spirv.data(), words.size() * sizeof(u32));

			valid = reflect_spirv(words.data(), words.size(), reflection);
			seAssert(valid, "Failed to reflect SPIR-V module");
			if (!valid)
			{
				return;
			}

			module = vk::raii::ShaderModule(device, vk::ShaderModuleCreateInfo({}, words.size() * sizeof(u32), words.data()));
		}
	}
}
