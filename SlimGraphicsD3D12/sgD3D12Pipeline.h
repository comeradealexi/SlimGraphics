#pragma once
#include "sgD3D12Include.h"
namespace sg
{
	namespace D3D12
	{
		class Pipeline
		{
			friend class Device;
		public:

		private:
			ComPtr<ID3D12PipelineState> pipeline;
		};
	}
}

