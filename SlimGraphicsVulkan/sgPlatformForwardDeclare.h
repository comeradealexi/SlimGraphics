#pragma once
#define SG_PLATFORM_VULKAN (1)

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
		class CommandList;
		class CommandQueue;
		class ComputeShader;
		class ConstantBufferView;
		class DepthStencilView;
		class Device;
		class GeometryShader;
		class GPUTimestampPool;
		class GPUStatisticPool;
		struct ImGuiTextureViewer;
		class IndexBufferView;
		class IndirectArguments;
		class Memory;
		class MeshShader;
		class Pipeline;
		class PixelShader;
		class QueueFence;
		class RenderTargetView;
		class ShaderResourceView;
		class Texture;
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
	using CommandList = Vulkan::CommandList;
	using CommandQueue = Vulkan::CommandQueue;
	using ComputeShader = Vulkan::ComputeShader;
	using ConstantBufferView = Vulkan::ConstantBufferView;
	using DepthStencilView = Vulkan::DepthStencilView;
	using Device = Vulkan::Device;
	using GeometryShader = Vulkan::GeometryShader;
	using GPUTimestampPool = Vulkan::GPUTimestampPool;
	using GPUStatisticPool = Vulkan::GPUStatisticPool;
	using ImGuiTextureViewer = Vulkan::ImGuiTextureViewer;
	using IndexBufferView = Vulkan::IndexBufferView;
	using IndirectArguments = Vulkan::IndirectArguments;
	using Memory = Vulkan::Memory;
	using MeshShader = Vulkan::MeshShader;
	using Pipeline = Vulkan::Pipeline;
	using PixelShader = Vulkan::PixelShader;
	using QueueFence = Vulkan::QueueFence;
	using RenderTargetView = Vulkan::RenderTargetView;
	using ShaderResourceView = Vulkan::ShaderResourceView;
	using Texture = Vulkan::Texture;
	using UnorderedAccessView = Vulkan::UnorderedAccessView;
	using VertexBufferView = Vulkan::VertexBufferView;
	using VertexShader = Vulkan::VertexShader;
}
