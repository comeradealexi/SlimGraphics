#pragma once
#include <sgTypes.h>

namespace sg
{
	namespace D3D12
	{
		class Buffer
		{
			friend class Device;
		public:
			Buffer(BufferType type_, bool cpu_write, bool cpu_read) : type(type_), cpu_writeable(cpu_write), cpu_readable(cpu_read) { }
			ComPtr<ID3D12Resource> get() { return resource; }
			BufferType get_type() const { return type; }

			void write_memory(u32 offset, const void* memory_src, u64 size);
			void read_memory(u32 offset, void* memory_dest, u64 size);

		protected:
			const BufferType type;
			const bool cpu_writeable;
			const bool cpu_readable;
			SharedPtr<Memory> memory;
			ComPtr<ID3D12Resource> resource;
		};
	}
}
