#pragma once
#include <sgTypes.h>
namespace sg
{
	namespace D3D12
	{
		class CommandList
		{
			friend class Device;
		public:
			CommandList(ComPtr<ID3D12GraphicsCommandList6>& in_command_list, ComPtr<ID3D12CommandAllocator>& in_command_allocator);
			
			void start_recording();
			void end_recording();
			void set_pipeline(Pipeline* pipeline);
			void bind(Binding& bind);
			void draw_instanced(u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location);
			void draw_indexed_instanced(u32 index_count_per_instance, u32 instance_count, u32 start_index_location, i8 base_vertex_location, u32 start_instance_location);

			void start_render_pass(u32 render_target_count, RenderTargetView* render_targets, DepthStencilView* depth_stencil = nullptr);
			void end_render_pass();
		private:
			Ptr<Device> device;
			ComPtr<ID3D12GraphicsCommandList6> command_list;
			ComPtr<ID3D12CommandAllocator> command_allocator;

			ComPtr<ID3D12DescriptorHeap> descriptor_heap;
			u32 descriptor_heap_index = 0;
			u32 descriptor_heap_increment = 0;
			u32 descriptor_heap_maximum = 0;
		};
	}
}
