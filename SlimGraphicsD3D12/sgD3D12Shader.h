#pragma once
#include "sgD3D12Include.h"
#include <vector>

namespace sg
{
	namespace D3D12
	{
		class Shader
		{
		public:
			Shader(const std::vector<uint8_t>& shader) : shader_binary_data(shader)
			{ 
				shader_code = CD3DX12_SHADER_BYTECODE(shader_binary_data.data(), shader_binary_data.size());
			}
			CD3DX12_SHADER_BYTECODE shader_code;
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
	}
}
