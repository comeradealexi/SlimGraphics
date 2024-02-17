#pragma once
#include "sgD3D12Include.h"
namespace sg
{
	namespace D3D12
	{
		class Pipeline
		{
			friend class Device;
			friend class CommandList;
		public:

		private:
			D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			ComPtr<ID3D12RootSignature> root_signature;
			ComPtr<ID3D12PipelineState> pipeline;
		};
	}
}

