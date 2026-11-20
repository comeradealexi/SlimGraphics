#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"
#include "sgVulkanBinding.h"
#include "sgVulkanRenderTargetView.h"
#include "sgVulkanDepthStencilView.h"
#include "sgVulkanVertexBufferView.h"
#include "sgVulkanIndexBufferView.h"
#include "sgVulkanResourceState.h"
#include "sgVulkanPipeline.h"

namespace sg
{
	namespace Vulkan
	{
		// Arguments for an indirect DispatchMesh (VkDrawMeshTasksIndirectCommandEXT layout: groupCountX/Y/Z)
		class IndirectArguments
		{
			friend class CommandList;
			friend class Device;
		public:
			void set_indirect_buffer(SharedPtr<Buffer> _indirect_buffer) { indirect_buffer = _indirect_buffer; }
		private:
			SharedPtr<Buffer> indirect_buffer;
		};

		// Records Vulkan commands behind the same interface as the D3D12 command list.
		//
		// Shaders are shader objects and all fixed function state is dynamic, so set_pipeline is a handful of state commands.
		// Descriptors live in the device's descriptor heaps: each command list owns a region of the resource heap that SRV / UAV
		// tables are written to (linearly, reset when recording restarts) and the table locations plus CBV addresses are passed
		// to shaders as push data, mirroring D3D12 descriptor tables and root CBVs.
		//
		// The D3D12 style API lets copies, clears, dispatches and barriers happen inside a "geometry pass". Vulkan does not allow
		// those inside a render pass instance, so dynamic rendering is begun lazily on the first draw and transparently ended
		// ("suspended") before any such command, then begun again (with LOAD) for the next draw.
		class CommandList
		{
			friend class Device;
		public:
			CommandList(Device& device, u32 queue_family);
			~CommandList();
			CommandList(const CommandList&) = delete;
			CommandList& operator=(const CommandList&) = delete;

			void start_recording();
			void end_recording();
			void set_pipeline(Pipeline* pipeline);

			void bind(Binding& bind, PipelineType type);
			void bind_vertex_buffer(VertexBufferView view);
			void bind_index_buffer(IndexBufferView view);
			void unbind_vertex_buffer() {}
			void unbind_index_buffer() {}

			void draw_instanced(u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location);
			void draw_indexed_instanced(u32 index_count_per_instance, u32 instance_count, u32 start_index_location, i32 base_vertex_location, u32 start_instance_location);

			void start_geometry_pass(u32 render_target_count, SharedPtr<RenderTargetView>* render_targets, const Viewport& viewport, const ScissorRect scissor, SharedPtr<DepthStencilView> depth_stencil = nullptr);
			void end_geometry_pass();

			void dispatch(u32 x = 1, u32 y = 1, u32 z = 1);
			void dispatch_mesh(u32 x = 1, u32 y = 1, u32 z = 1);
			void execute_indirect(IndirectArguments& indirect_argument);

			void clear_buffer_float(UnorderedAccessView& uav, ShaderResourceView& srv, float value);
			void clear_buffer_uint(UnorderedAccessView& uav, ShaderResourceView& srv, sg::u32 value);

			void copy_buffer_to_buffer(Buffer* dest, Buffer* source);
			void copy_texture_to_texture(Texture& dest, Texture& source);
			void copy_buffer_to_buffer(u32 size, Buffer* dest, u32 dest_offset, Buffer* source, u32 source_offset);
			void copy_buffer_to_texture(u32 size, Texture* dest, Buffer* source, u32 source_offset, u32 subresource_index = 0);
			// Copies subresource 0 of a texture into a (readback) buffer with rows aligned to 256 bytes. Returns the row pitch in bytes.
			u32 copy_texture_to_buffer(Buffer* dest, Texture* source);

			void clear_render_target_view(SharedPtr<RenderTargetView>& rtv, float4 colour);
			void clear_depth_stencil_view(SharedPtr<DepthStencilView>& dsv, bool clear_depth, bool clear_stencil, float depth_value = 1.0f, u8 stencil_value = 0);

			// Vulkan specific
			vk::CommandBuffer get_command_buffer() const { return *command_buffer; }
			const vk::raii::CommandBuffer& get() const { return command_buffer; }
			void write_timestamp(vk::QueryPool pool, u32 query, vk::PipelineStageFlags2 stage);
			// Pipeline statistics queries are recorded as segments (see GPUStatisticPool)
			void begin_statistics_query(GPUStatisticPool* pool, u32 logical_index);
			void end_statistics_query();
			// Makes sure dynamic rendering is active for the current geometry pass (e.g. before third party rendering such as ImGui)
			void ensure_rendering();
			// Third party code changed pipeline/descriptor/viewport state behind our back
			void invalidate_state();

		private:
			struct PassState
			{
				bool active = false;
				u32 render_target_count = 0;
				SharedPtr<RenderTargetView> render_targets[8];
				SharedPtr<DepthStencilView> depth_stencil;
				vk::Rect2D render_area;
				vk::Viewport viewport;
				vk::Rect2D scissor;
				// Clears requested before rendering began are folded into the attachment load ops
				bool clear_colour[8] = {};
				vk::ClearColorValue clear_colour_value[8];
				bool clear_depth = false;
				bool clear_stencil = false;
				vk::ClearDepthStencilValue clear_depth_stencil_value;
			};

			void suspend_rendering();
			// Records all pending transitions (and optionally the pending UAV memory barrier) in one barrier batch
			void flush_barriers(bool include_uav_barrier);
			// Brings shader, dynamic state, descriptor heap and push data state up to date before a draw / dispatch
			void flush_shader_state();
			void bind_shaders();
			void set_graphics_state();
			void set_vertex_input();
			void write_descriptor_tables();
			void set_viewport_scissor();
			void ensure_bindings_valid();
			void mark_uavs_written();
			// Switches the active statistics segment between vertex pipeline and mesh work
			void update_statistics_mode(bool mesh);

			// Barrier helpers: transitions are queued (merging repeated transitions of the same resource) and flushed together
			void transition(Buffer* buffer, ResourceState before, ResourceState after);
			void transition(Texture* texture, ResourceState before, ResourceState after);
			ResourceState get_current_state(Buffer* buffer) const;
			ResourceState get_current_state(Texture* texture) const;
			void retain(const SharedPtr<Buffer>& buffer);
			void retain(const SharedPtr<Texture>& texture);

			struct PendingTransition
			{
				Buffer* buffer = nullptr;
				Texture* texture = nullptr;
				ResourceState before = ResourceState::Undefined;
				ResourceState after = ResourceState::Undefined;
			};

			Device& device;
			vk::PipelineStageFlags2 shader_stages;
			vk::raii::CommandPool command_pool = nullptr;
			vk::raii::CommandBuffer command_buffer = nullptr;

			Pipeline* active_pipeline = nullptr;
			Binding active_binding;
			u64 binding_version = 0;

			// State that must be (re)recorded before the next draw / dispatch
			bool heaps_dirty = true;
			bool shaders_dirty = false;
			bool vertex_input_dirty = true;
			bool push_data_dirty = true;
			bool viewport_dirty = true;
			// Binding and pipeline the current descriptor tables were written for
			u64 tables_binding_version = ~0ull;
			const Pipeline* tables_pipeline = nullptr;
			PushData push_data;
			u64 vertex_buffer_stride = 0;

			// This command list's region of the resource descriptor heap: [region_first_slot, region_first_slot + COMMAND_LIST_DESCRIPTOR_SLOTS)
			u32 region_first_slot = 0;
			u32 region_used_slots = 0;

			bool uav_barrier_pending = false;
			bool rendering = false;
			PassState pass;
			std::vector<PendingTransition> pending_transitions;

			struct ActiveStatistics
			{
				GPUStatisticPool* pool = nullptr;
				u32 logical_index = 0;
				bool mesh_mode = false;
				bool segment_open = false;
			} statistics;

			// Keeps resources referenced by recorded commands alive until the command buffer is reused (the next start_recording
			// happens after the GPU finished with it), so application code can drop resources mid-frame safely.
			std::vector<SharedPtr<Buffer>> retained_buffers;
			std::vector<SharedPtr<Texture>> retained_textures;
		};
	}
}
