#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"
#include "sgVulkanSpirvReflection.h"
#include <vector>

namespace sg
{
	namespace Vulkan
	{
		// A SPIR-V module (compiled by DXC, see SlimShaderCompiler) plus its reflection data. Shader objects are created from
		// it when a sg::Pipeline is created, so stages can be linked together.
		class Shader
		{
		public:
			explicit Shader(const std::vector<uint8_t>& spirv);

			const std::vector<u32>& get_code() const { return code; }
			const SpirvReflection& get_reflection() const { return reflection; }
			vk::ShaderStageFlagBits get_stage() const { return reflection.stage; }
			const char* get_entry_point() const { return reflection.entry_point.c_str(); }
			bool is_valid() const { return valid; }

		private:
			std::vector<u32> code;
			SpirvReflection reflection;
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
