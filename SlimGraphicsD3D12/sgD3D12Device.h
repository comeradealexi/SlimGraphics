#pragma once
#include <sgTypes.h>
#include "sgD3D12Include.h"
#include "sgD3D12Memory.h"

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
			static constexpr u64 ADAPTER_MEMORY_TO_CONSUME_PERCENTAGE = 70;
			static constexpr u64 TEXTURE_POOL_PERCENTAGE = 40;
			static constexpr u64 TARGET_POOL_PERCENTAGE = 40;
			static constexpr u64 BUFFER_POOL_PERCENTAGE = 20;
			static_assert(TEXTURE_POOL_PERCENTAGE + TARGET_POOL_PERCENTAGE + BUFFER_POOL_PERCENTAGE == 100, "Pool sizes must equal 100%");

		public:
			Device();
			Ptr<Memory> allocate_memory(MemoryType type, MemorySubType sub_type, u64 size, u64 alignment);

			Ptr<CommandQueue> create_command_queue();
			Ptr<CommandList> create_command_buffer();

			//Memory
		private:
			ComPtr<ID3D12Device> device;
			ComPtr<ID3D12Device6> device6;
			CD3DX12FeatureSupport features;

			AllocatorPIMPL allocator;
			PoolPIMPL mempool_textures;
			PoolPIMPL mempool_targets;
			PoolPIMPL mempool_buffers;
		};
	}
}