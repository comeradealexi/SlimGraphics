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

		void begin_frame(sg::CommandQueue* queue);
		Offset allocate_upload_memory(sg::u32 size, sg::u32 alignment);
		void write_upload_memory(Offset offset, const void* memory, sg::u32 size);
		void upload_to_buffer(sg::Buffer* dest_buffer, sg::u32 dest_byte_offset, Offset upload_heap_offset, sg::u32 size);
		void upload_to_texture(sg::Texture* dest_texture, Offset upload_heap_offset, sg::u32 size);
		void upload_to_texture(sg::Texture* dest_texture, sg::u32 texture_mip_index, Offset upload_heap_offset, sg::u32 size);

		void end_frame(sg::CommandQueue* queue);

	private:
		sg::u32 counter = 0;

		struct PerFrameData
		{
			sg::SharedPtr<sg::CommandList> command_list;
			sg::SharedPtr<sg::Buffer> buffer;
			ComPtr<sg::QueueFence> fence;
			sg::u32 upload_heap_size = 0;
			sg::u32 fence_wait_value = 0;
			bool fence_signalled = false;

			sg::u32 upload_heap_offset = 0;
		};
		std::vector<PerFrameData> per_frame_list;
		PerFrameData& frame_data() { return per_frame_list[counter % (sg::u32)per_frame_list.size()]; }
	};
}