#include "pch.h"
#include "sgD3D12CommandList.h"
namespace sg
{
	namespace D3D12
	{
		CommandList::CommandList(ComPtr<ID3D12GraphicsCommandList6>& in_command_list, ComPtr<ID3D12CommandAllocator>& in_command_allocator) :
			command_list(in_command_list),
			command_allocator(in_command_allocator)
		{

		}

		void CommandList::StartRecording()
		{
			CHECKHR(command_list->Reset(command_allocator.Get(), nullptr));
		}

		void CommandList::EndRecording()
		{
			CHECKHR(command_list->Close());
		}
	}
}
