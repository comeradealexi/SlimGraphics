#pragma once
#include "sgD3D12Buffer.h"

namespace sg
{
	namespace D3D12
	{
		class Texture : public Buffer
		{
			friend class Device;
		public:
			Texture(bool uav_access, const ResourceCreateDesc& rcd) : resource_create_desc(rcd), Buffer(BufferType::Texture, 0, uav_access, false, false) { }
			Texture(size_t size_bytes_, bool uav_access_, bool cpu_write, bool cpu_read, const ResourceCreateDesc& rcd) : resource_create_desc(rcd), Buffer(BufferType::Texture, size_bytes_, uav_access_, cpu_write, cpu_read) { }
			
			const ResourceCreateDesc resource_create_desc;
		};
	}
}
