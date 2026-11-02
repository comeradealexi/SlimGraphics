#pragma once
#include <sgTypes.h>

namespace sg
{
	namespace Vulkan
	{
		class Buffer
		{
			friend class Device;
		public:
			Buffer(BufferType type_, size_t size_bytes_, bool uav_access_, bool cpu_write, bool cpu_read) : type(type_), size_bytes(size_bytes_), uav_access(uav_access_), cpu_writeable(cpu_write), cpu_readable(cpu_read) {}
#if SG_PLATFORM_D3D12
			ComPtr<ID3D12Resource> get() { return resource; }
#endif			BufferType get_type() const { return type; }
			MemoryType get_memory_type() const;

#if SG_PLATFORM_D3D12
			virtual D3D12_RESOURCE_STATES get_read_resource_state() const;
#endif
			size_t get_size_bytes() const { return size_bytes;  }

			void* map_memory();
			void unmap_memory();

			void write_memory(u32 offset, const void* memory_src, u64 size);
			void read_memory(u32 offset, void* memory_dest, u64 size);

		protected:
			const BufferType type;
			const size_t size_bytes;
			const bool uav_access;
			const bool cpu_writeable;
			const bool cpu_readable;
			SharedPtr<Memory> memory;
#if SG_PLATFORM_D3D12
			ComPtr<ID3D12Resource> resource;
#endif		
		};
	}
}
