#pragma once
#include <sgPlatformInclude.h>
#include <vector>

class UploadHeap
{
public:
	UploadHeap(sg::Device* device, sg::u32 buffer_count, sg::u32 size_per_frame);

	void begin_frame();
	void end_frame_and_submit();

private:
	sg::u32 active_index = 0;
	sg::Ptr<sg::CommandList> command_buffer;
	std::vector<sg::SharedPtr<sg::Buffer>> buffer_list;
	std::vector<ComPtr<sg::QueueFence>> fence_list;
};

