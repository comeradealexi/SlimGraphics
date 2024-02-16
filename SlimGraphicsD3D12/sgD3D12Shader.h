#pragma once

namespace sg
{
	namespace D3D12
	{
		class Shader
		{
		public:
			Shader(CD3DX12_SHADER_BYTECODE code) : shader_code(code) { }
			
		private:
			CD3DX12_SHADER_BYTECODE shader_code;
		};

		class ComputeShader : public Shader
		{
		public:
			ComputeShader(CD3DX12_SHADER_BYTECODE code) : Shader(code) { }
		};

		class VertexShader : public Shader
		{
		public:
			VertexShader(CD3DX12_SHADER_BYTECODE code) : Shader(code) { }

		};

		class PixelShader : public Shader
		{
		public:
			PixelShader(CD3DX12_SHADER_BYTECODE code) : Shader(code) { }
		};
	}
}
