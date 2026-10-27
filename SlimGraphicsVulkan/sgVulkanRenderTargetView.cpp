#include "pch.h"
#include "sgD3D12RenderTargetView.h"
#include "sgD3D12Device.h"

namespace sg
{
	namespace D3D12
	{
		RenderTargetView::~RenderTargetView()
		{
			heap->free(heap_index);
		}
	}
}