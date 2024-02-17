#pragma once
#include <sgTypes.h>
#include "sgD3D12Include.h"
#include "sgD3D12Memory.h"
#include "sgD3D12Shader.h"
#include "sgD3D12DescriptorHeap.h"

namespace sg
{
	namespace D3D12
	{
		struct AllocatorPIMPL
		{
			AllocatorPIMPL();
			~AllocatorPIMPL();
			inline D3D12MA::Allocator* operator->()
			{
				return ptr.Get();
			}
			ComPtr<D3D12MA::Allocator> ptr;
		};

		struct PoolPIMPL
		{
			PoolPIMPL();
			~PoolPIMPL();
			inline D3D12MA::Pool* operator->()
			{
				return ptr.Get();
			}
			ComPtr<D3D12MA::Pool> ptr;
		};

		class Device
		{
		public:
			static constexpr u64 DESCRIPTOR_COUNT = 1024;
			static constexpr u64 ADAPTER_MEMORY_TO_CONSUME_PERCENTAGE = 70;
			static constexpr u64 TEXTURE_POOL_PERCENTAGE = 40;
			static constexpr u64 TARGET_POOL_PERCENTAGE = 40;
			static constexpr u64 BUFFER_POOL_PERCENTAGE = 20;
			static_assert(TEXTURE_POOL_PERCENTAGE + TARGET_POOL_PERCENTAGE + BUFFER_POOL_PERCENTAGE == 100, "Pool sizes must equal 100%");

		public:
			Device();
			Ptr<Memory> allocate_memory(MemoryType type, MemorySubType sub_type, u64 size, u64 alignment);
			ComPtr<QueueFence> create_queue_fence();

			Ptr<CommandQueue> create_command_queue();
			Ptr<CommandList> create_command_buffer();

			Ptr<VertexShader> create_vertex_shader(std::vector<uint8_t>& shader);
			Ptr<PixelShader> create_pixel_shader(std::vector<uint8_t>& shader);

			bool create_swap_chain(HWND hwnd, CommandQueue* command_queue, u32 buffer_count, DXGI_FORMAT format, u32 width, u32 height, RenderTargetView* rtv_list);

			Ptr<Pipeline> create_pipeline(const PipelineDesc::Graphics& pipeline_desc, const BindingDesc& binding_desc);

			//D3D12 Specific
		public:
			ComPtr<ID3D12DescriptorHeap> get_cbv_srv_uav_descriptor_heap();
			ComPtr<ID3D12DescriptorHeap> get_rtv_descriptor_heap();
			u32 get_rtv_descriptor_heap_increment_size();

			ComPtr<ID3D12Device> get_device() { return device; }

		private:
			void create_descriptors();
			
		private:
			ComPtr<ID3D12Device> device;
			ComPtr<ID3D12Device6> device6;
			ComPtr<IDXGIFactory4> factory;
			ComPtr<IDXGISwapChain1> swap_chain;
			CD3DX12FeatureSupport features;

			AllocatorPIMPL allocator;
			PoolPIMPL mempool_textures;
			PoolPIMPL mempool_targets;
			PoolPIMPL mempool_buffers;

			Ptr<DescriptorHeap> cbv_srv_uav_descriptor_heap;
			Ptr<DescriptorHeap> rtv_descriptor_heap;
			Ptr<DescriptorHeap> dsv_descriptor_heap;
			Ptr<DescriptorHeap> sampler_descriptor_heap;
		};
	}
}