#include "pch.h"
#include "sgD3D12Binding.h"

namespace sg
{
	namespace D3D12
	{
		void Binding::set_cbv(ConstantBufferView& cbv, u32 index)
		{
			d3d12_cbvs[index] = cbv;
			//BaseBinding::set_cbv(cbv.cbv, index);
		}

		void Binding::set_srv(ShaderResourceView& srv, u32 index)
		{
			d3d12_srvs[index] = srv;
			//BaseBinding::set_srv(srv.srv, index);
		}

		void Binding::set_uav(UnorderedAccessView& uav, u32 index)
		{
			d3d12_uavs[index] = uav;
			//BaseBinding::set_uav(uav.uav, index);
		}
	}
}