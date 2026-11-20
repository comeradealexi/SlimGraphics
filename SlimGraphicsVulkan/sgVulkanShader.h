#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"
#include "sgVulkanSpirvReflection.h"
#include <vector>

namespace sg
{
	namespace Vulkan
	{
		// A SPIR-V module (compiled by DXC, see SlimShaderCompiler) plus its reflection data
		class Shader
		{
		public:
			Shader(const vk::raii::Device& device, const std::vector<uint8_t>& spirv);

			vk::ShaderModule get_module() const { return *module; }
			const SpirvReflection& get_reflection() const { return reflection; }
			vk::ShaderStageFlagBits get_stage() const { return reflection.stage; }
			const char* get_entry_point() const { return reflection.entry_point.c_str(); }
			bool is_valid() const { return valid; }

		private:
			SpirvReflection reflection;
			vk::raii::ShaderModule module = nullptr;
			bool valid = false;
		};

		class ComputeShader : public Shader { public: using Shader::Shader; };
		class VertexShader : public Shader { public: using Shader::Shader; };
		class PixelShader : public Shader { public: using Shader::Shader; };
		class MeshShader : public Shader { public: using Shader::Shader; };
		class AmplificationShader : public Shader { public: using Shader::Shader; };
		class GeometryShader : public Shader { public: using Shader::Shader; };
	}
}
