#include "pch.h"
#include "sgVulkanShader.h"

namespace sg
{
	namespace Vulkan
	{
		Shader::Shader(const std::vector<uint8_t>& spirv)
		{
			seAssert(spirv.size() % 4 == 0 && !spirv.empty(), "SPIR-V size must be a non-zero multiple of 4 bytes");
			// Copy into word storage so the code is correctly aligned for parsing and shader creation
			code.resize(spirv.size() / 4);
			memcpy(code.data(), spirv.data(), code.size() * sizeof(u32));
			valid = reflect_spirv(code.data(), code.size(), reflection);
			seAssert(valid, "Failed to reflect SPIR-V module");
		}
	}
}
