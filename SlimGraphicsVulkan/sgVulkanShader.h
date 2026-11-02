#pragma once
#include "sgVulkanInclude.h"
#include <vector>

namespace sg
{
	namespace Vulkan
	{
		class Shader
		{
		public:
			Shader(const std::vector<uint8_t>& shader) : shader_binary_data(shader)
			{ 
				seAssert(false, "TODO");
#if SG_PLATFORM_D3D12
				shader_code = CD3DX12_SHADER_BYTECODE(shader_binary_data.data(), shader_binary_data.size());
#endif
			}
#if SG_PLATFORM_D3D12
			CD3DX12_SHADER_BYTECODE shader_code;
#endif
			std::vector<uint8_t> shader_binary_data;
		};

		class ComputeShader : public Shader
		{
		public:
			ComputeShader(const std::vector<uint8_t>& shader) : Shader(shader) { }
		};

		class VertexShader : public Shader
		{
		public:
			VertexShader(const std::vector<uint8_t>& shader) : Shader(shader) { }

		};

		class PixelShader : public Shader
		{
		public:
			PixelShader(const std::vector<uint8_t>& shader) : Shader(shader) { }
		};

		class MeshShader : public Shader
		{
		public:
			MeshShader(const std::vector<uint8_t>& shader) : Shader(shader) { }
		};

		class AmplificationShader : public Shader
		{
		public:
			AmplificationShader(const std::vector<uint8_t>& shader) : Shader(shader) { }
		};

		class GeometryShader : public Shader
		{
		public:
			GeometryShader(const std::vector<uint8_t>& shader) : Shader(shader) {}
		};
	}
}
