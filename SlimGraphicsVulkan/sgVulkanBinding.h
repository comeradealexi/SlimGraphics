#pragma once
#include <sgTypes.h>
#include "sgVulkanUnorderedAccessView.h"
#include "sgVulkanConstantBufferView.h"
#include "sgVulkanShaderResourceView.h"

namespace sg
{
	namespace Vulkan
	{
		// Samplers come from the device's global sampler list (sampler i -> register s<i>), as in the D3D12 backend.
		class Binding : public BaseBinding
		{
		public:
			void set_cbv(ConstantBufferView& cbv, u32 index) { cbvs[index] = cbv; }
			void set_srv(ShaderResourceView& srv, u32 index) { srvs[index] = srv; }
			void set_uav(UnorderedAccessView& uav, u32 index) { uavs[index] = uav; }

			ConstantBufferView cbvs[MAX_CBVS];
			ShaderResourceView srvs[MAX_SRVS];
			UnorderedAccessView uavs[MAX_UAVS];
		};
	}
}
