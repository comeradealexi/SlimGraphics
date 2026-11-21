#pragma once
#include <sgTypes.h>
#include "sgD3D12Binding.h"
#include "sgD3D12RenderTargetView.h"
#include "sgD3D12DepthStencilView.h"
#include "sgD3D12VertexBufferView.h"
#include "sgD3D12IndexBufferView.h"

namespace sg
{
	namespace D3D12
	{
		class DescriptorHeap;

		class IndirectArguments
		{
			friend class CommandList;
			friend class Device;
		public:
			void set_indirect_buffer(SharedPtr<Buffer> _indirect_buffer)
			{
				indirect_buffer = _indirect_buffer;
			}

		private:
			SharedPtr<Buffer> indirect_buffer;
			ComPtr<ID3D12CommandSignature> command_signature;
		};

		class CommandList
		{
			friend class Device;
		public:
			CommandList(ComPtr<ID3D12GraphicsCommandList6>& in_command_list, ComPtr<ID3D12CommandAllocator>& in_command_allocator);
			
			void start_recording();
			void end_recording();
			void set_pipeline(Pipeline* pipeline);
			
			void bind(Binding& bind, PipelineType type);
			void bind_vertex_buffer(VertexBufferView view);
			void bind_index_buffer(IndexBufferView view);
			void unbind_vertex_buffer();
			void unbind_index_buffer();

			void draw_instanced(u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location);
			void draw_indexed_instanced(u32 index_count_per_instance, u32 instance_count, u32 start_index_location, i32 base_vertex_location, u32 start_instance_location);

			void start_geometry_pass(u32 render_target_count, SharedPtr<RenderTargetView>* render_targets, const Viewport& viewport, const ScissorRect scissor, SharedPtr<DepthStencilView> depth_stencil = nullptr);
			void end_geometry_pass();

			void dispatch(u32 x = 1, u32 y = 1, u32 z = 1);
			void dispatch_mesh(u32 x = 1, u32 y = 1, u32 z = 1);
			// argument_offset: byte offset of the arguments in the indirect buffer (a multiple of 4)
			void execute_indirect(IndirectArguments& indirect_argument, u64 argument_offset = 0);

			// Ray tracing acceleration structure builds. 'dest' is a BufferType::AccelerationStructure buffer of at least the
			// prebuild result size (from dest_offset, a multiple of 256, so many structures can share a buffer), 'scratch' a
			// GeneralDataBuffer with UAV access of at least the scratch size. The build is followed by a UAV barrier, so later
			// builds and ray queries see it.
			void build_bottom_level_acceleration_structure(const Raytracing::GeometryDesc* geometries, u32 geometry_count, Buffer* dest, Buffer* scratch, u64 dest_offset = 0);
			// 'instances' holds instance_count Raytracing::InstanceDesc and rests in its read state
			void build_top_level_acceleration_structure(Buffer* instances, u32 instance_count, Buffer* dest, Buffer* scratch);

			// Clears a texture UAV (all of the view's mip) to a value, e.g. history and accumulation targets
			void clear_texture_float(UnorderedAccessView& uav, float4 value);

			void clear_buffer_float(UnorderedAccessView& uav, ShaderResourceView& srv, float value);
			void clear_buffer_uint(UnorderedAccessView& uav, ShaderResourceView& srv, sg::u32 value);

			void copy_buffer_to_buffer(Buffer* dest, Buffer* source);
			void copy_texture_to_texture(Texture& dest, Texture& source);
			void copy_buffer_to_buffer(u32 size, Buffer* dest, u32 dest_offset, Buffer* source, u32 source_offset);
			void copy_buffer_to_texture(u32 size, Texture* dest, Buffer* source, u32 source_offset, u32 texture_mip_index = 0);
			// Copies subresource 0 of a texture into a (readback) buffer with rows aligned to 256 bytes. Returns the row pitch in bytes.
			u32 copy_texture_to_buffer(Buffer* dest, Texture* source);

			void clear_render_target_view(SharedPtr<RenderTargetView>& rtv, float4 colour);

			void clear_depth_stencil_view(SharedPtr<DepthStencilView>& dsv, bool clear_depth, bool clear_stencil, float depth_value = 1.0f, u8 stencil_value = 0);
			ComPtr<ID3D12GraphicsCommandList6> get() { return command_list; };

		private:
			void flush_bound_uavs();
			// Copies the device's bindless descriptors to the start of the heap when they changed since the last copy
			// (descriptors allocated while recording must be visible to the work recorded after them)
			void refresh_bindless_descriptors();
			void ensure_bindings_valid();
			D3D12_RESOURCE_STATES get_current_state(Buffer* buffer);
			void transition(Buffer* buffer, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

			ComPtr<ID3D12GraphicsCommandList6> command_list;
			ComPtr<ID3D12CommandAllocator> command_allocator;

			ComPtr<ID3D12DescriptorHeap> descriptor_heap;
			u32 descriptor_heap_index = 0;
			u32 descriptor_heap_increment = 0;
			u32 descriptor_heap_maximum = 0;
			u32 bindless_copied_version = ~0u;

			ComPtr<ID3D12DescriptorHeap> non_shader_descriptor_heap;

			ComPtr<ID3D12Device> device;
			Device* sg_device = nullptr;
			u32 descriptor_increment_size_cbv_srv_uav = 0;
			u32 descriptor_increment_size_rtv = 0;
			u32 descriptor_increment_size_dsv = 0;
			u32 descriptor_increment_size_sampler = 0;

			//Binding Info
			Pipeline* active_pipeline = nullptr;
			Binding active_binding;
			struct ActiveGeometryPass
			{
				SharedPtr<RenderTargetView> rtvs[8];
				SharedPtr<DepthStencilView> dsv;
				VertexBufferView vbv;
				IndexBufferView ibv;
			} active_geometry_pass;
		};
	}
}
