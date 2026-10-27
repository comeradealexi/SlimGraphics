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
			Texture(bool uav_access, const ResourceCreateDesc& rcd, bool is_swap_chain_buffer = false) : resource_create_desc(rcd), Buffer(BufferType::Texture, 0, uav_access, false, false), swap_chain_buffer(is_swap_chain_buffer) { }
			Texture(size_t size_bytes_, bool uav_access_, bool cpu_write, bool cpu_read, const ResourceCreateDesc& rcd, bool is_swap_chain_buffer = false) : resource_create_desc(rcd), Buffer(BufferType::Texture, size_bytes_, uav_access_, cpu_write, cpu_read), swap_chain_buffer(is_swap_chain_buffer) { }
			
			virtual D3D12_RESOURCE_STATES get_read_resource_state() const override
			{
				if (swap_chain_buffer)
				{
					return D3D12_RESOURCE_STATE_PRESENT;
				}
				return Buffer::get_read_resource_state();
			}

			const bool swap_chain_buffer = false;
			const ResourceCreateDesc resource_create_desc;
		};
	}
}
