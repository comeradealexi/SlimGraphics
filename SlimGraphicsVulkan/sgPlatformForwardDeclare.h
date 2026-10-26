#pragma once
#define SG_PLATFORM_VULKAN (1)

struct ID3D12Fence;

namespace sg
{
	namespace DefaultAlignment
	{
		static constexpr size_t BUFFER_ALIGNMENT = 1024llu * 64llu;
		static constexpr size_t CONSTANT_BUFFER_ALIGNMENT = 256u;
	}

	namespace Vulkan
	{
		class AmplificationShader;
		class Binding;
		class Buffer;
		class BufferView;
		class CommandList;
		class CommandQueue;
		class ComputeShader;
		class ConstantBufferView;
		class DepthStencilView;
		class Device;
		class GeometryShader;
		class GPUTimestampPool;
		class GPUStatisticPool;
		class IndexBufferView;
		class IndirectArguments;
		class Memory;
		class MeshShader;
		class Pipeline;
		class PixelShader;
		class RenderTargetView;
		class Sampler;
		class ShaderResourceView;
		class Texture;
		class TextureView;
		class UnorderedAccessView;
		class VertexBufferView;
		class VertexShader;
	}
}

namespace sg
{
	using AmplificationShader = Vulkan::AmplificationShader;
	using Binding = Vulkan::Binding;
	using Buffer = Vulkan::Buffer;
	using BufferView = Vulkan::BufferView;
	using CommandList = Vulkan::CommandList;
	using CommandQueue = Vulkan::CommandQueue;
	using ComputeShader = Vulkan::ComputeShader;
	using ConstantBufferView = Vulkan::ConstantBufferView;
	using DepthStencilView = Vulkan::DepthStencilView;
	using Device = Vulkan::Device;
	using GeometryShader = Vulkan::GeometryShader;
	using GPUTimestampPool = Vulkan::GPUTimestampPool;
	using GPUStatisticPool = Vulkan::GPUStatisticPool;
	using IndexBufferView = Vulkan::IndexBufferView;
	using IndirectArguments = Vulkan::IndirectArguments;
	using Memory = Vulkan::Memory;
	using MeshShader = Vulkan::MeshShader;
	using Pipeline = Vulkan::Pipeline;
	using PixelShader = Vulkan::PixelShader;
	using QueueFence = ::ID3D12Fence;
	using RenderTargetView = Vulkan::RenderTargetView;
	using Sampler = Vulkan::Sampler;
	using ShaderResourceView = Vulkan::ShaderResourceView;
	using Texture = Vulkan::Texture;
	using TextureView = Vulkan::TextureView;
	using UnorderedAccessView = Vulkan::UnorderedAccessView;
	using VertexBufferView = Vulkan::VertexBufferView;
	using VertexShader = Vulkan::VertexShader;

}

namespace D3D12MA
{
	class Allocation;
	class Allocator;
	class Pool;
	class VirtualBlock;
	struct VirtualAllocation;
}