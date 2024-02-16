#pragma once
#include <sgTypes.h>
#include "sgD3D12Include.h"

namespace sg
{
	namespace D3D12
	{
		class CommandQueue
		{
		public:
			CommandQueue(ComPtr<ID3D12CommandQueue>& in_queue);
			ComPtr<ID3D12CommandQueue> get() { return queue; }

		private:
			ComPtr<ID3D12CommandQueue> queue;
		};
	}
}
