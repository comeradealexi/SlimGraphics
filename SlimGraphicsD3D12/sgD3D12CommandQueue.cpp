#include "pch.h"
#include "sgD3D12CommandQueue.h"


namespace sg
{
	namespace D3D12
	{
		CommandQueue::CommandQueue(ComPtr<ID3D12CommandQueue>& in_queue) : queue(in_queue)
		{

		}
	}
}