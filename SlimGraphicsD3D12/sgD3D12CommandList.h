#pragma once
#include <sgTypes.h>
namespace sg
{
	namespace D3D12
	{
		class CommandList
		{
			friend class Device;
		public:
			CommandList(ComPtr<ID3D12GraphicsCommandList6>& in_command_list, ComPtr<ID3D12CommandAllocator>& in_command_allocator);
			
			void startRecording();
			void endRecording();
			void bind(Binding& bind);
		private:
			Ptr<Device> device;
			ComPtr<ID3D12GraphicsCommandList6> command_list;
			ComPtr<ID3D12CommandAllocator> command_allocator;

			ComPtr<ID3D12DescriptorHeap> descriptor_heap;
			u32 descriptor_heap_index = 0;
			u32 descriptor_heap_increment = 0;
			u32 descriptor_heap_maximum = 0;
		};
	}
}
