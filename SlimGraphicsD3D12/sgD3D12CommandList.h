#pragma once

namespace sg
{
	namespace D3D12
	{
		class CommandList
		{
		public:
			CommandList(ComPtr<ID3D12GraphicsCommandList6>& in_command_list, ComPtr<ID3D12CommandAllocator>& in_command_allocator);
			
			void StartRecording();
			void EndRecording();
		private:

			ComPtr<ID3D12GraphicsCommandList6> command_list;
			ComPtr<ID3D12CommandAllocator> command_allocator;
		};
	}
}
