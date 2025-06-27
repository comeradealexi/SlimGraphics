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
		struct PoolPIMPL
		{
			PoolPIMPL();
			~PoolPIMPL();
			inline ID3D12Heap* operator->()
			{
				return heap.Get();
			}
			ComPtr<D3D12MA::VirtualBlock> virtual_block;
			ComPtr<ID3D12Heap> heap;
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
			void imgui_init(u32 num_frames, DXGI_FORMAT format, DXGI_FORMAT depth_format, CommandQueue* command_queue);
			void imgui_render(CommandList* command_list);

			D3D12_RESOURCE_DESC create_dx12_resource_desc(const ResourceCreateDesc& desc);
			SizeAndAlignment calculate_resource_size_alignment(const ResourceCreateDesc& desc);

			SharedPtr<Memory> allocate_memory(MemoryType type, MemorySubType sub_type, u64 size, u64 alignment);
			ComPtr<QueueFence> create_queue_fence();

			Ptr<GPUTimestampPool> create_gpu_timestamp_pool(CommandQueue* queue, u32 max_timestamps);
			Ptr<GPUStatisticPool> create_gpu_statistic_pool(CommandQueue* queue, u32 max_stats);

			Ptr<CommandQueue> create_command_queue();
			Ptr<CommandList> create_command_buffer();

			Ptr<VertexShader> create_vertex_shader(const std::vector<uint8_t>& shader);
			Ptr<PixelShader> create_pixel_shader(const std::vector<uint8_t>& shader);
			Ptr<ComputeShader> create_compute_shader(const std::vector<uint8_t>& shader);
			Ptr<MeshShader> create_mesh_shader(const std::vector<uint8_t>& shader);

			//Returns current index to use
			u32 create_swap_chain(HWND hwnd, CommandQueue* command_queue, u32 buffer_count, DXGI_FORMAT format, u32 width, u32 height, RenderTargetView* rtv_list);
			u32 present_swap_chain(CommandQueue* command_queue);

			Ptr<Pipeline> create_pipeline(const PipelineDesc::Graphics& pipeline_desc, const BindingDesc& binding_desc);
			Ptr<Pipeline> create_pipeline(const PipelineDesc::Compute& pipeline_desc, const BindingDesc& binding_desc);
			Ptr<Pipeline> create_pipeline(const PipelineDesc::Mesh& pipeline_desc, const BindingDesc& binding_desc);

			SharedPtr<Buffer> create_buffer(SharedPtr<Memory> memory, u32 size, u32 alignment, BufferType type, bool uav_access);
			SharedPtr<Texture> create_texture(SharedPtr<Memory> memory, u32 size, u32 alignment, const ResourceCreateDesc& resource_desc);

			// DXGI_FORMAT_UNKNOWN means use default target format
			RenderTargetView create_render_target_view(SharedPtr<Texture>& texture, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

			// DXGI_FORMAT_UNKNOWN means use default target format
			DepthStencilView create_depth_stencil_view(SharedPtr<Texture>& texture, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

			ConstantBufferView create_constant_buffer_view(Buffer* buffer, u64 offset, u64 size);
			ShaderResourceView create_shader_resource_view(SharedPtr<Buffer> buffer, u64 element_size, u64 element_count);
			UnorderedAccessView create_unordered_access_view(SharedPtr<Buffer> buffer, u64 element_size, u64 element_count);
			VertexBufferView create_vertex_buffer_view(SharedPtr<Buffer> buffer, u64 offset, u64 size, u64 stride);
			IndexBufferView create_index_buffer_view(SharedPtr<Buffer> buffer, u64 offset, u64 size, DXGI_FORMAT format);

			//Support checks
			bool SupportsMeshShaders();
			size_t GetMeshShaderMaxNumThreads() { return 128; };
			size_t GetMeshShaderMaxOutputVerts() { return 256; };
			size_t GetMeshShaderMaxOutputPrims() { return 256; };

			bool SupportsWaveOps();
			u32 GetWaveLaneCountMin();
			u32 GetWaveLaneCountMax();
			u32 GetTotalLaneCount();

			//D3D12 Specific
		public:
			ComPtr<ID3D12DescriptorHeap> get_cbv_srv_uav_descriptor_heap();
			ComPtr<ID3D12DescriptorHeap> get_rtv_descriptor_heap();
			u32 get_rtv_descriptor_heap_increment_size();

			ComPtr<ID3D12Device> get_device() { return device; }

		private:
			ComPtr<ID3D12RootSignature> create_root_signature(const BindingDesc& binding_desc, bool compute);
			void create_descriptors();
			
		private:
			ComPtr<ID3D12Device> device;
			ComPtr<ID3D12Device6> device6;
			ComPtr<IDXGIFactory4> factory;
			ComPtr<IDXGISwapChain1> swap_chain;
			u32 swap_chain_current_index = 0;
			u32 swap_chain_buffer_count = 0;
			CD3DX12FeatureSupport features;

			PoolPIMPL mempool_textures;
			PoolPIMPL mempool_targets;
			PoolPIMPL mempool_buffers;

			SharedPtr<DescriptorHeap> cbv_srv_uav_descriptor_heap;
			SharedPtr<DescriptorHeap> cbv_srv_uav_descriptor_heap_imgui;
			SharedPtr<DescriptorHeap> rtv_descriptor_heap;
			SharedPtr<DescriptorHeap> dsv_descriptor_heap;
			SharedPtr<DescriptorHeap> sampler_descriptor_heap;
		};
	}
}