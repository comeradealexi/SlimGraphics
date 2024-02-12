#pragma once
#define SG_PLATFORM_D3D12 (1)

namespace sg
{
	namespace D3D12
	{
		class Buffer;
		class BufferView;
		class CommandBuffer;
		class CommandQueue;
		class ComputeShader;
		class Device;
		class Memory;
		class Pipeline;
		class PixelShader;
		class Texture;
		class TextureView;
		class VertexShader;
	}
}

namespace sg
{
	using Buffer = D3D12::Buffer;
	using BufferView = D3D12::BufferView;
	using CommandBuffer = D3D12::CommandBuffer;
	using CommandQueue = D3D12::CommandQueue;
	using ComputeShader = D3D12::ComputeShader;
	using Device = D3D12::Device;
	using Memory = D3D12::Memory;
	using PipeLine = D3D12::Pipeline;
	using PixelShader = D3D12::PixelShader;
	using Texture = D3D12::Texture;
	using TextureView = D3D12::TextureView;
	using VertexShader = D3D12::VertexShader;
}