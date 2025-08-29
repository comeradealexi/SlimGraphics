#include "pch.h"
#include "sgD3D12DepthStencilView.h"
#include "sgD3D12Device.h"

namespace sg
{
	namespace D3D12
	{
		DepthStencilView::~DepthStencilView()
		{
			heap->free(heap_index);
		}
	}
}