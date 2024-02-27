#pragma once
#define SG_PLATFORM_D3D12 (1)

struct ID3D12Fence;

namespace sg
{
	namespace D3D12
	{
		class Buffer;
		class BufferView;
		class CommandList;
		class CommandQueue;
		class ComputeShader;
		class ConstantBufferView;
		class DepthStencilView;
		class Device;
		class GPUTimestampPool;
		class Memory;
		class Pipeline;
		class PixelShader;
		class RenderTargetView;
		class Sampler;
		class ShaderResourceView;
		class Texture;
		class TextureView;
		class UnorderedAccessView;
		class VertexShader;
		class Binding;
	}
}

namespace sg
{
	using Buffer = D3D12::Buffer;
	using BufferView = D3D12::BufferView;
	using CommandList = D3D12::CommandList;
	using CommandQueue = D3D12::CommandQueue;
	using ComputeShader = D3D12::ComputeShader;
	using ConstantBufferView = D3D12::ConstantBufferView;
	using DepthStencilView = D3D12::DepthStencilView;
	using Device = D3D12::Device;
	using GPUTimestampPool = D3D12::GPUTimestampPool;
	using Memory = D3D12::Memory;
	using Pipeline = D3D12::Pipeline;
	using PixelShader = D3D12::PixelShader;
	using QueueFence = ::ID3D12Fence;
	using RenderTargetView = D3D12::RenderTargetView;
	using Sampler = D3D12::Sampler;
	using ShaderResourceView = D3D12::ShaderResourceView;
	using Texture = D3D12::Texture;
	using TextureView = D3D12::TextureView;
	using UnorderedAccessView = D3D12::UnorderedAccessView;
	using VertexShader = D3D12::VertexShader;
	using Binding = D3D12::Binding;
}

namespace D3D12MA
{
	class Allocation;
	class Allocator;
	class Pool;
}