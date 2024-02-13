#pragma once
#include <sgTypes.h>
#include "sgD3D12Include.h"
#include "sgD3D12Memory.h"

namespace sg
{
	namespace D3D12
	{
		class Device
		{
		public:
			Device();
			Ptr<Memory> allocate_memory(MemoryType type, u64 size, u64 alignment);

			Ptr<CommandQueue> create_command_queue();
			Ptr<CommandList> create_command_buffer();

		private:
			ComPtr<ID3D12Device> device;
			ComPtr<ID3D12Device6> device6;
			CD3DX12FeatureSupport features;
		};
	}
}