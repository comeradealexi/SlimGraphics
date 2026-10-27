#pragma once
#include <sgTypes.h>
#include "sgD3D12UnorderedAccessView.h"
#include "sgD3D12ConstantBufferView.h"
#include "sgD3D12ShaderResourceView.h"

namespace sg
{
	namespace D3D12
	{
		class Binding : public BaseBinding
		{
		public:
			void set_cbv(ConstantBufferView& cbv, u32 index);
			void set_srv(ShaderResourceView& srv, u32 index);
			void set_uav(UnorderedAccessView& uav, u32 index);

			ConstantBufferView d3d12_cbvs[MAX_CBVS];
			ShaderResourceView d3d12_srvs[MAX_SRVS];
			UnorderedAccessView d3d12_uavs[MAX_UAVS];
		};
	}
}
