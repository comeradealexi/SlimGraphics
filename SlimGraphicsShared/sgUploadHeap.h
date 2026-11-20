#pragma once
#include <sgPlatformInclude.h>
#include <vector>

namespace sg
{
	class UploadHeap
	{
	public:
		using Offset = sg::u32;
		static constexpr Offset INVALID_OFFSET = (Offset)~0;
		UploadHeap(sg::Device* device, sg::u32 buffer_count, sg::u32 size_per_frame);

		void begin_frame(sg::SharedPtr<sg::CommandQueue> queue);
		Offset allocate_upload_memory(sg::u32 size, sg::u32 alignment);
		void write_upload_memory(Offset offset, const void* memory, sg::u32 size);
		void upload_to_buffer(sg::Buffer* dest_buffer, sg::u32 dest_byte_offset, Offset upload_heap_offset, sg::u32 size);
		void upload_to_texture(sg::Texture* dest_texture, Offset upload_heap_offset, sg::u32 size);
		void upload_to_texture(sg::Texture* dest_texture, sg::u32 texture_mip_index, Offset upload_heap_offset, sg::u32 size);

		// Copies tightly packed texture rows into upload memory using the row pitch the GPU copy expects
		// (row size aligned to 256 bytes) and records the copy into the given subresource.
		// tight_row_pitch: bytes per row (or per block row for compressed formats). row_count: total rows (block rows * depth slices).
		void upload_texture_subresource(sg::Texture* dest_texture, sg::u32 subresource_index, const void* data, sg::u32 tight_row_pitch, sg::u32 row_count);

		void end_frame();

	private:
		// Starts at 1: fences are created with a completed value of 0, so signalling 0 would make the first wait a no-op.
		sg::u32 counter = 1;
		struct PerFrameData
		{
			void* mapped_gpu_memory = nullptr;
			sg::SharedPtr<sg::CommandQueue> queue_ref;
			sg::SharedPtr<sg::CommandList> command_list;
			sg::SharedPtr<sg::Buffer> buffer;
			sg::SharedPtr<sg::QueueFence> fence;
			sg::u32 upload_heap_size = 0;
			sg::u32 fence_wait_value = 0;
			bool fence_signalled = false;

			sg::u32 upload_heap_offset = 0;
		};
		std::vector<PerFrameData> per_frame_list;
		PerFrameData& frame_data() { return per_frame_list[counter % (sg::u32)per_frame_list.size()]; }
	};
}