#pragma once
#include <sgTypes.h>
#include "sgD3D12Binding.h"
#include "sgD3D12RenderTargetView.h"
#include "sgD3D12DepthStencilView.h"
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
			void bind(Binding& bind, PipelineType type);
			void draw_instanced(u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location);
			void draw_indexed_instanced(u32 index_count_per_instance, u32 instance_count, u32 start_index_location, i8 base_vertex_location, u32 start_instance_location);

			void start_geometry_pass(u32 render_target_count, RenderTargetView* render_targets, const Viewport& viewport, const ScissorRect scissor, bool rtv0_is_swap_chain = false, DepthStencilView* depth_stencil = nullptr);
			void end_geometry_pass();

			void dispatch(u32 x = 1, u32 y = 1, u32 z = 1);

			void copy_buffer_to_buffer(Buffer* dest, Buffer* source);
			void copy_buffer_to_buffer(u32 size, Buffer* dest, u32 dest_offset, Buffer* source, u32 source_offset);

			void clear_render_target_view(RenderTargetView rtv, float4 colour);

			ComPtr<ID3D12GraphicsCommandList6> get() { return command_list; };

		private:
			void flush_bound_uavs();

			ComPtr<ID3D12GraphicsCommandList6> command_list;
			ComPtr<ID3D12CommandAllocator> command_allocator;

			ComPtr<ID3D12DescriptorHeap> descriptor_heap;
			u32 descriptor_heap_index = 0;
			u32 descriptor_heap_increment = 0;
			u32 descriptor_heap_maximum = 0;

			ComPtr<ID3D12Device> device;
			ComPtr<ID3D12DescriptorHeap> global_cbv_srv_uav_descriptor_heap;
			ComPtr<ID3D12DescriptorHeap> global_rtv_descriptor_heap;
			ComPtr<ID3D12DescriptorHeap> global_dsv_descriptor_heap;
			ComPtr<ID3D12DescriptorHeap> global_sampler_descriptor_heap;
			u32 descriptor_increment_size_cbv_srv_uav = 0;
			u32 descriptor_increment_size_rtv = 0;
			u32 descriptor_increment_size_dsv = 0;
			u32 descriptor_increment_size_sampler = 0;

			//Binding Info
			Binding active_binding;
			struct ActiveGeometryPass
			{
				RenderTargetView rtvs[8];
				DepthStencilView dsv;
				bool rtv0_is_swap_chain = false;;
			} active_geometry_pass;
		};
	}
}
