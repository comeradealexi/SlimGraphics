#include "pch.h"
#include "sgVulkanCommandList.h"
#include "sgVulkanDevice.h"
#include "sgVulkanPipeline.h"
#include "sgVulkanTypesTranslator.h"
#include "sgVulkanGPUStatisticPool.h"
#include "sgUtils.h"

namespace sg
{
	namespace Vulkan
	{
		namespace
		{
			constexpr u32 kTextureRowPitchAlignment = 256; // Matches UploadHeap::upload_texture_subresource / D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
		}

		CommandList::CommandList(Device& device_) : device(device_), shader_stages(device_.get_shader_stages())
		{
			const vk::raii::Device& vk_device = device.get_device();
			command_pool = CHECKVK(vk_device.createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eTransient, device.get_queue_family())));
			std::vector<vk::raii::CommandBuffer> buffers = CHECKVK(vk_device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(*command_pool, vk::CommandBufferLevel::ePrimary, 1)));
			command_buffer = std::move(buffers[0]);
			region_first_slot = device.allocate_descriptor_region();
		}

		CommandList::~CommandList()
		{
			device.free_descriptor_region(region_first_slot);
		}

		void CommandList::start_recording()
		{
			// The previous submission of this command list has completed (the caller waited on its fence), so resetting the pool
			// and releasing the resources it referenced is safe.
			CHECKVK(command_pool.reset());
			retained_buffers.clear();
			retained_textures.clear();

			active_pipeline = nullptr;
			active_binding = Binding();
			binding_version = 0;
			heaps_dirty = true;
			shaders_dirty = false;
			vertex_input_dirty = true;
			push_data_dirty = true;
			viewport_dirty = true;
			graphics_state_valid = false;
			tables_binding_version = ~0ull;
			tables_pipeline = nullptr;
			push_data = PushData();
			vertex_buffer_stride = 0;
			region_used_slots = 0; // The GPU finished reading the descriptors written by the previous recording
			uav_barrier_pending = false;
			rendering = false;
			pass = PassState();
			pending_transitions.clear();
			statistics = ActiveStatistics();

			CHECKVK(command_buffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)));
		}

		void CommandList::end_recording()
		{
			seAssert(!pass.active, "end_recording called inside a geometry pass");
			end_statistics_query();
			suspend_rendering();

			// Leave every resource in its resting state so the next command list starts from a known state
			Binding default_binding;
			bind(default_binding, PipelineType::Compute);
			flush_barriers(true);

			CHECKVK(command_buffer.end());
		}

		void CommandList::set_pipeline(Pipeline* pipeline)
		{
			seAssert(pipeline != nullptr && !pipeline->shaders.empty(), "Invalid pipeline");
			if (pipeline == active_pipeline && !shaders_dirty)
			{
				return;
			}
			active_pipeline = pipeline;
			bind_shaders();
		}

		void CommandList::bind(Binding& bind, PipelineType)
		{
			// Graphics and compute share one set of push data / descriptor tables, written for whichever shaders are bound.
			// Transition UAVs that are no longer bound back to their read state and newly bound ones to UNORDERED_ACCESS.
			// Work on unique resources so a resource that stays bound (in another slot or via another mip view) is untouched.
			struct ResourceRef
			{
				const void* identity;
				Buffer* buffer;
				Texture* texture;
			};
			auto collect = [](const Binding& b, ResourceRef* out) -> u32
				{
					u32 count = 0;
					for (u32 i = 0; i < b.uav_binding_count; i++)
					{
						const UnorderedAccessView& uav = b.uavs[i];
						const void* identity = uav.get_resource_identity();
						if (!identity) continue;
						bool seen = false;
						for (u32 j = 0; j < count && !seen; j++) seen = out[j].identity == identity;
						if (!seen) out[count++] = { identity, uav.buffer_resource.get(), uav.texture_resource.get() };
					}
					return count;
				};
			auto contains = [](const ResourceRef* list, u32 count, const void* identity)
				{
					for (u32 i = 0; i < count; i++) if (list[i].identity == identity) return true;
					return false;
				};

			ResourceRef old_resources[Binding::MAX_UAVS];
			ResourceRef new_resources[Binding::MAX_UAVS];
			const u32 old_count = collect(active_binding, old_resources);
			const u32 new_count = collect(bind, new_resources);

			for (u32 i = 0; i < old_count; i++)
			{
				if (!contains(new_resources, new_count, old_resources[i].identity))
				{
					if (old_resources[i].buffer) transition(old_resources[i].buffer, ResourceState::UnorderedAccess, old_resources[i].buffer->get_read_resource_state());
					else transition(old_resources[i].texture, ResourceState::UnorderedAccess, old_resources[i].texture->get_read_resource_state());
				}
			}
			for (u32 i = 0; i < new_count; i++)
			{
				if (!contains(old_resources, old_count, new_resources[i].identity))
				{
					if (new_resources[i].buffer) transition(new_resources[i].buffer, new_resources[i].buffer->get_read_resource_state(), ResourceState::UnorderedAccess);
					else transition(new_resources[i].texture, new_resources[i].texture->get_read_resource_state(), ResourceState::UnorderedAccess);
				}
			}

			// Keep everything referenced by this binding alive until the command buffer has executed
			for (u32 i = 0; i < bind.srv_binding_count; i++)
			{
				retain(bind.srvs[i].buffer_resource);
				retain(bind.srvs[i].texture_resource);
			}
			for (u32 i = 0; i < bind.uav_binding_count; i++)
			{
				retain(bind.uavs[i].buffer_resource);
				retain(bind.uavs[i].texture_resource);
			}

			active_binding = bind;
			binding_version++;
		}

		void CommandList::bind_vertex_buffer(VertexBufferView view)
		{
			retain(view.buffer_resource);
			const vk::Buffer buffer = view.buffer_resource->get();
			const vk::DeviceSize offset = view.offset;
			const vk::DeviceSize size = view.size;
			command_buffer.bindVertexBuffers2(0, buffer, offset, size, {});
			// The stride is part of the vertex input state (vkCmdSetVertexInputEXT), as in D3D12 vertex buffer views
			if (view.stride != vertex_buffer_stride)
			{
				vertex_buffer_stride = view.stride;
				vertex_input_dirty = true;
			}
		}

		void CommandList::bind_index_buffer(IndexBufferView view)
		{
			retain(view.buffer_resource);
			command_buffer.bindIndexBuffer(view.buffer_resource->get(), view.offset, view.index_type);
		}

		void CommandList::prepare_draw(bool mesh)
		{
			ensure_bindings_valid();
			update_statistics_mode(mesh);
			flush_barriers(!rendering); // Inside rendering, UAV writes are ordered by the draw sequence (as with D3D12 without UAV barriers)
			ensure_rendering();
			flush_shader_state();
		}

		void CommandList::prepare_dispatch()
		{
			ensure_bindings_valid();
			prepare_transfer(); // Dispatches, like copies, run outside render pass instances
			flush_shader_state();
		}

		void CommandList::prepare_transfer()
		{
			suspend_rendering();
			flush_barriers(true);
		}

		void CommandList::draw_instanced(u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location)
		{
			prepare_draw(false);
			command_buffer.draw(vertex_count_per_instance, instance_count, start_vertex_location, start_instance_location);
			mark_uavs_written();
		}

		void CommandList::draw_indexed_instanced(u32 index_count_per_instance, u32 instance_count, u32 start_index_location, i32 base_vertex_location, u32 start_instance_location)
		{
			prepare_draw(false);
			command_buffer.drawIndexed(index_count_per_instance, instance_count, start_index_location, base_vertex_location, start_instance_location);
			mark_uavs_written();
		}

		void CommandList::start_geometry_pass(u32 render_target_count, SharedPtr<RenderTargetView>* render_targets, const Viewport& viewport, const ScissorRect scissor, SharedPtr<DepthStencilView> depth_stencil)
		{
			seAssert(!pass.active, "Geometry passes cannot be nested");
			seAssert(render_target_count <= GraphicsState::MAX_RENDER_TARGETS, "Too many render targets");
			suspend_rendering();

			pass = PassState();
			pass.active = true;
			pass.render_target_count = render_target_count;
			vk::Extent3D extent(1, 1, 1);
			for (u32 i = 0; i < render_target_count; i++)
			{
				pass.render_targets[i] = render_targets[i];
				Texture* texture = render_targets[i]->texture_resource.get();
				retain(render_targets[i]->texture_resource);
				transition(texture, texture->get_read_resource_state(), ResourceState::RenderTarget);
				if (i == 0) extent = texture->get_mip_extent(0);
			}
			if (depth_stencil)
			{
				pass.depth_stencil = depth_stencil;
				Texture* texture = depth_stencil->texture_resource.get();
				retain(depth_stencil->texture_resource);
				transition(texture, texture->get_read_resource_state(), ResourceState::DepthWrite);
				if (render_target_count == 0) extent = texture->get_mip_extent(0);
			}

			pass.render_area = vk::Rect2D({ 0, 0 }, { extent.width, extent.height });
			// Negative height flips Y so clip space matches D3D (shaders are shared and not compiled with -fvk-invert-y)
			pass.viewport = vk::Viewport(viewport.top_left_x, viewport.top_left_y + viewport.height, viewport.width, -viewport.height, viewport.min_depth, viewport.max_depth);
			const u32 scissor_width = scissor.right > scissor.left ? scissor.right - scissor.left : 0;
			const u32 scissor_height = scissor.bottom > scissor.top ? scissor.bottom - scissor.top : 0;
			pass.scissor = vk::Rect2D({ static_cast<i32>(scissor.left), static_cast<i32>(scissor.top) }, { scissor_width, scissor_height });
			viewport_dirty = true;
		}

		void CommandList::end_geometry_pass()
		{
			seAssert(pass.active, "end_geometry_pass without start_geometry_pass");

			// Apply clears that were requested but never followed by a draw
			bool pending_clear = pass.clear_depth || pass.clear_stencil;
			for (u32 i = 0; i < pass.render_target_count; i++) pending_clear |= pass.clear_colour[i];
			if (pending_clear)
			{
				ensure_rendering();
			}
			suspend_rendering();

			for (u32 i = 0; i < pass.render_target_count; i++)
			{
				Texture* texture = pass.render_targets[i]->texture_resource.get();
				transition(texture, ResourceState::RenderTarget, texture->get_read_resource_state());
			}
			if (pass.depth_stencil)
			{
				Texture* texture = pass.depth_stencil->texture_resource.get();
				transition(texture, ResourceState::DepthWrite, texture->get_read_resource_state());
			}
			pass = PassState();

			//To allow bound resources to be transitioned back to a read state.
			Binding default_binding;
			bind(default_binding, PipelineType::Geometry);
		}

		void CommandList::dispatch(u32 x, u32 y, u32 z)
		{
			prepare_dispatch();
			command_buffer.dispatch(x, y, z);
			mark_uavs_written();
		}

		void CommandList::dispatch_mesh(u32 x, u32 y, u32 z)
		{
			prepare_draw(true);
			command_buffer.drawMeshTasksEXT(x, y, z);
			mark_uavs_written();
		}

		void CommandList::execute_indirect(IndirectArguments& indirect_argument, u64 argument_offset)
		{
			// Only used for indirect DispatchMesh (see Device::create_indirect_arguments). The arguments are usually written by
			// a compute shader, so pending UAV writes are always made visible (unlike other draws inside a pass).
			Buffer* buffer = indirect_argument.indirect_buffer.get();
			retain(indirect_argument.indirect_buffer);
			const ResourceState state = get_current_state(buffer);
			transition(buffer, state, ResourceState::IndirectArgument);
			flush_barriers(true);
			prepare_draw(true);
			command_buffer.drawMeshTasksIndirectEXT(buffer->get(), argument_offset, 1, sizeof(VkDrawMeshTasksIndirectCommandEXT));
			transition(buffer, ResourceState::IndirectArgument, state);
			mark_uavs_written();
		}

		void CommandList::clear_buffer_float(UnorderedAccessView& uav, ShaderResourceView& srv, float value)
		{
			// Clears a 32-bit element buffer; the float's bit pattern produces identical memory contents
			sg::u32 bits = 0;
			static_assert(sizeof(bits) == sizeof(value), "size mismatch");
			memcpy(&bits, &value, sizeof(bits));
			clear_buffer_uint(uav, srv, bits);
		}

		void CommandList::clear_buffer_uint(UnorderedAccessView& uav, ShaderResourceView&, sg::u32 value)
		{
			// The SRV is only needed by D3D12 (ClearUnorderedAccessViewUint takes a CPU descriptor); vkCmdFillBuffer needs neither
			seAssert(uav.buffer_resource, "clear_buffer_uint requires a buffer UAV");
			Buffer* buffer = uav.buffer_resource.get();
			retain(uav.buffer_resource);

			const ResourceState state = get_current_state(buffer);
			transition(buffer, state, ResourceState::CopyDest);
			prepare_transfer();

			// Same range as the D3D12 raw R32 clear view: the UAV's elements, in whole 32-bit words
			const u64 view_size = uav.get_size_bytes() & ~3ull;
			const vk::DeviceSize size = (view_size == 0 || view_size >= buffer->get_size_bytes()) ? VK_WHOLE_SIZE : view_size;
			command_buffer.fillBuffer(buffer->get(), 0, size, value);

			transition(buffer, ResourceState::CopyDest, state);
		}

		void CommandList::copy_buffer_to_buffer(Buffer* dest, Buffer* source)
		{
			seAssert(dest && source, "no valid buffers");
			seAssert(dest->get_size_bytes() >= source->get_size_bytes(), "Dest too small for source size");
			copy_buffer_to_buffer(static_cast<u32>(std::min(dest->get_size_bytes(), source->get_size_bytes())), dest, 0, source, 0);
		}

		void CommandList::copy_buffer_to_buffer(u32 size, Buffer* dest, u32 dest_offset, Buffer* source, u32 source_offset)
		{
			seAssert(dest && source, "no valid buffers");
			seAssert(dest_offset + static_cast<u64>(size) <= dest->get_size_bytes(), "Dest buffer not big enough");
			seAssert(source_offset + static_cast<u64>(size) <= source->get_size_bytes(), "Source buffer not big enough");

			const ResourceState state_dest = get_current_state(dest);
			const ResourceState state_source = get_current_state(source);
			transition(dest, state_dest, ResourceState::CopyDest);
			transition(source, state_source, ResourceState::CopySource);
			prepare_transfer();

			command_buffer.copyBuffer(source->get(), dest->get(), vk::BufferCopy(source_offset, dest_offset, size));

			transition(dest, ResourceState::CopyDest, state_dest);
			transition(source, ResourceState::CopySource, state_source);
		}

		void CommandList::copy_texture_to_texture(Texture& dest, Texture& source)
		{
			const ResourceState state_dest = get_current_state(&dest);
			const ResourceState state_source = get_current_state(&source);
			transition(&dest, state_dest, ResourceState::CopyDest);
			transition(&source, state_source, ResourceState::CopySource);
			prepare_transfer();

			if (dest.get_vk_format() == source.get_vk_format())
			{
				// Equivalent of CopyResource: every mip and layer
				const u32 mip_count = std::min(dest.resource_create_desc.mip_count, source.resource_create_desc.mip_count);
				const u32 layer_count = std::min(dest.get_layer_count(), source.get_layer_count());
				std::vector<vk::ImageCopy2> regions;
				for (u32 mip = 0; mip < mip_count; mip++)
				{
					regions.push_back(vk::ImageCopy2(
						vk::ImageSubresourceLayers(source.get_aspect(), mip, 0, layer_count), {},
						vk::ImageSubresourceLayers(dest.get_aspect(), mip, 0, layer_count), {},
						source.get_mip_extent(mip)));
				}
				command_buffer.copyImage2(vk::CopyImageInfo2(source.get(), vk::ImageLayout::eTransferSrcOptimal, dest.get(), vk::ImageLayout::eTransferDstOptimal, regions));
			}
			else
			{
				// Different formats (e.g. an RGBA render target into a BGRA swap chain image): blit converts the channels
				const vk::Extent3D src_extent = source.get_mip_extent(0);
				const vk::Extent3D dst_extent = dest.get_mip_extent(0);
				const vk::ImageBlit2 region(
					vk::ImageSubresourceLayers(source.get_aspect(), 0, 0, 1), { vk::Offset3D(0, 0, 0), vk::Offset3D(static_cast<i32>(src_extent.width), static_cast<i32>(src_extent.height), 1) },
					vk::ImageSubresourceLayers(dest.get_aspect(), 0, 0, 1), { vk::Offset3D(0, 0, 0), vk::Offset3D(static_cast<i32>(dst_extent.width), static_cast<i32>(dst_extent.height), 1) });
				command_buffer.blitImage2(vk::BlitImageInfo2(source.get(), vk::ImageLayout::eTransferSrcOptimal, dest.get(), vk::ImageLayout::eTransferDstOptimal, region, vk::Filter::eNearest));
			}

			transition(&dest, ResourceState::CopyDest, state_dest);
			transition(&source, ResourceState::CopySource, state_source);
		}

		void CommandList::copy_buffer_to_texture(u32 size, Texture* dest, Buffer* source, u32 source_offset, u32 subresource_index)
		{
			seAssert(dest && source, "no valid resources");
			const ResourceCreateDesc& rcd = dest->resource_create_desc;
			const u32 mip_count = std::max(1u, rcd.mip_count);
			const u32 mip = subresource_index % mip_count;
			const u32 layer = subresource_index / mip_count;
			const vk::Extent3D extent = dest->get_mip_extent(mip);

			// Source rows are laid out with the row size aligned to 256 bytes (UploadHeap::upload_texture_subresource)
			const FormatInfo info = get_format_info(rcd.format);
			const u32 blocks_wide = (extent.width + info.block_width - 1) / info.block_width;
			const u32 blocks_high = (extent.height + info.block_height - 1) / info.block_height;
			const u32 row_pitch = AlignUp(blocks_wide * info.block_bytes, kTextureRowPitchAlignment);
			seAssert(row_pitch % info.block_bytes == 0, "Row pitch must be a whole number of texel blocks");
			seAssert(static_cast<u64>(row_pitch) * blocks_high * extent.depth <= size, "Upload data smaller than the texture subresource");

			const ResourceState state_dest = get_current_state(dest);
			transition(dest, state_dest, ResourceState::CopyDest);
			prepare_transfer();

			const vk::BufferImageCopy2 region(source_offset, (row_pitch / info.block_bytes) * info.block_width, blocks_high * info.block_height,
				vk::ImageSubresourceLayers(dest->get_aspect(), mip, layer, 1), {}, extent);
			command_buffer.copyBufferToImage2(vk::CopyBufferToImageInfo2(source->get(), dest->get(), vk::ImageLayout::eTransferDstOptimal, region));

			transition(dest, ResourceState::CopyDest, state_dest);
		}

		u32 CommandList::copy_texture_to_buffer(Buffer* dest, Texture* source)
		{
			seAssert(dest && source, "no valid resources");
			const FormatInfo info = get_format_info(source->resource_create_desc.format);
			const vk::Extent3D extent = source->get_mip_extent(0);
			const u32 blocks_wide = (extent.width + info.block_width - 1) / info.block_width;
			const u32 blocks_high = (extent.height + info.block_height - 1) / info.block_height;
			const u32 row_pitch = AlignUp(blocks_wide * info.block_bytes, kTextureRowPitchAlignment);
			seAssert(dest->get_size_bytes() >= static_cast<u64>(row_pitch) * blocks_high, "Destination buffer too small for texture copy");

			const ResourceState state_dest = get_current_state(dest);
			const ResourceState state_source = get_current_state(source);
			transition(dest, state_dest, ResourceState::CopyDest);
			transition(source, state_source, ResourceState::CopySource);
			prepare_transfer();

			const vk::BufferImageCopy2 region(0, (row_pitch / info.block_bytes) * info.block_width, blocks_high * info.block_height,
				vk::ImageSubresourceLayers(source->get_aspect(), 0, 0, 1), {}, extent);
			command_buffer.copyImageToBuffer2(vk::CopyImageToBufferInfo2(source->get(), vk::ImageLayout::eTransferSrcOptimal, dest->get(), region));

			transition(dest, ResourceState::CopyDest, state_dest);
			transition(source, ResourceState::CopySource, state_source);
			return row_pitch;
		}

		void CommandList::clear_render_target_view(SharedPtr<RenderTargetView>& rtv, float4 colour)
		{
			const vk::ClearColorValue clear_value(std::array<float, 4>{ colour[0], colour[1], colour[2], colour[3] });

			// Render target of the current pass: fold into the load op, or clear the attachment if rendering already started
			for (u32 i = 0; pass.active && i < pass.render_target_count; i++)
			{
				if (pass.render_targets[i]->texture_resource == rtv->texture_resource)
				{
					if (rendering)
					{
						const vk::ClearAttachment attachment(vk::ImageAspectFlagBits::eColor, i, vk::ClearValue(clear_value));
						command_buffer.clearAttachments(attachment, vk::ClearRect(pass.render_area, 0, 1));
					}
					else
					{
						pass.clear_colour[i] = true;
						pass.clear_colour_value[i] = clear_value;
					}
					return;
				}
			}

			// Not an attachment of the current pass: clear with a transfer operation
			Texture* texture = rtv->texture_resource.get();
			retain(rtv->texture_resource);
			const ResourceState state = get_current_state(texture);
			transition(texture, state, ResourceState::CopyDest);
			prepare_transfer();
			command_buffer.clearColorImage(texture->get(), vk::ImageLayout::eTransferDstOptimal, clear_value, texture->get_full_range());
			transition(texture, ResourceState::CopyDest, state);
		}

		void CommandList::clear_depth_stencil_view(SharedPtr<DepthStencilView>& dsv, bool clear_depth, bool clear_stencil, float depth_value, u8 stencil_value)
		{
			Texture* texture = dsv->texture_resource.get();
			clear_stencil = clear_stencil && has_stencil(texture->resource_create_desc.format);
			if (!clear_depth && !clear_stencil)
			{
				return;
			}
			const vk::ClearDepthStencilValue clear_value(depth_value, stencil_value);

			if (pass.active && pass.depth_stencil && pass.depth_stencil->texture_resource == dsv->texture_resource)
			{
				if (rendering)
				{
					vk::ImageAspectFlags aspect;
					if (clear_depth) aspect |= vk::ImageAspectFlagBits::eDepth;
					if (clear_stencil) aspect |= vk::ImageAspectFlagBits::eStencil;
					const vk::ClearAttachment attachment(aspect, 0, vk::ClearValue(clear_value));
					command_buffer.clearAttachments(attachment, vk::ClearRect(pass.render_area, 0, 1));
				}
				else
				{
					pass.clear_depth |= clear_depth;
					pass.clear_stencil |= clear_stencil;
					pass.clear_depth_stencil_value = clear_value;
				}
				return;
			}

			retain(dsv->texture_resource);
			const ResourceState state = get_current_state(texture);
			transition(texture, state, ResourceState::CopyDest);
			prepare_transfer();
			vk::ImageSubresourceRange range = texture->get_full_range();
			range.aspectMask = vk::ImageAspectFlags();
			if (clear_depth) range.aspectMask |= vk::ImageAspectFlagBits::eDepth;
			if (clear_stencil) range.aspectMask |= vk::ImageAspectFlagBits::eStencil;
			command_buffer.clearDepthStencilImage(texture->get(), vk::ImageLayout::eTransferDstOptimal, clear_value, range);
			transition(texture, ResourceState::CopyDest, state);
		}

		void CommandList::write_timestamp(vk::QueryPool pool, u32 query, vk::PipelineStageFlags2 stage)
		{
			command_buffer.writeTimestamp2(stage, pool, query);
		}

		void CommandList::begin_statistics_query(GPUStatisticPool* pool, u32 logical_index)
		{
			seAssert(statistics.pool == nullptr, "A statistics query is already active");
			// Segments begin outside of rendering so they may span render pass instances (which are suspended/resumed freely)
			suspend_rendering();
			statistics = ActiveStatistics();
			statistics.pool = pool;
			statistics.logical_index = logical_index;
			statistics.segment_open = pool->begin_segment(command_buffer, logical_index, false);
		}

		void CommandList::end_statistics_query()
		{
			if (!statistics.pool)
			{
				return;
			}
			suspend_rendering();
			if (statistics.segment_open)
			{
				statistics.pool->end_segment(command_buffer);
			}
			statistics = ActiveStatistics();
		}

		void CommandList::update_statistics_mode(bool mesh)
		{
			if (!statistics.pool || statistics.mesh_mode == mesh)
			{
				return;
			}
			suspend_rendering();
			if (statistics.segment_open)
			{
				statistics.pool->end_segment(command_buffer);
			}
			statistics.mesh_mode = mesh;
			statistics.segment_open = statistics.pool->begin_segment(command_buffer, statistics.logical_index, mesh);
		}

		void CommandList::ensure_rendering()
		{
			if (rendering || !pass.active)
			{
				return;
			}
			flush_barriers(true);

			std::array<vk::RenderingAttachmentInfo, GraphicsState::MAX_RENDER_TARGETS> colour_attachments;
			for (u32 i = 0; i < pass.render_target_count; i++)
			{
				RenderTargetView& rtv = *pass.render_targets[i];
				vk::RenderingAttachmentInfo& attachment = colour_attachments[i];
				attachment.imageView = rtv.texture_resource->get_attachment_view(rtv.format);
				attachment.imageLayout = vk::ImageLayout::eAttachmentOptimal;
				attachment.loadOp = pass.clear_colour[i] ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
				attachment.storeOp = vk::AttachmentStoreOp::eStore;
				attachment.clearValue = vk::ClearValue(pass.clear_colour_value[i]);
				pass.clear_colour[i] = false;
			}

			vk::RenderingAttachmentInfo depth_attachment;
			vk::RenderingAttachmentInfo stencil_attachment;
			vk::RenderingInfo rendering_info({}, pass.render_area, 1, 0, pass.render_target_count, colour_attachments.data());
			if (pass.depth_stencil)
			{
				DepthStencilView& dsv = *pass.depth_stencil;
				const vk::ImageView view = dsv.texture_resource->get_attachment_view(dsv.format);
				depth_attachment = vk::RenderingAttachmentInfo(view, vk::ImageLayout::eAttachmentOptimal, {}, {}, {},
					pass.clear_depth ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, vk::ClearValue(pass.clear_depth_stencil_value));
				rendering_info.pDepthAttachment = &depth_attachment;
				if (has_stencil(dsv.texture_resource->resource_create_desc.format))
				{
					stencil_attachment = depth_attachment;
					stencil_attachment.loadOp = pass.clear_stencil ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
					rendering_info.pStencilAttachment = &stencil_attachment;
				}
				pass.clear_depth = false;
				pass.clear_stencil = false;
			}

			command_buffer.beginRendering(rendering_info);
			rendering = true;
		}

		void CommandList::invalidate_state()
		{
			// Third party rendering (ImGui) binds its own pipeline (overwriting dynamic state) and descriptor sets / push
			// constants, which invalidate descriptor heap and push data state
			heaps_dirty = true;
			push_data_dirty = true;
			shaders_dirty = active_pipeline != nullptr;
			graphics_state_valid = false;
			vertex_input_dirty = true;
			viewport_dirty = true;
		}

		void CommandList::suspend_rendering()
		{
			if (rendering)
			{
				command_buffer.endRendering();
				rendering = false;
			}
		}

		void CommandList::flush_barriers(bool include_uav_barrier)
		{
			const bool uav_barrier = include_uav_barrier && uav_barrier_pending;
			if (pending_transitions.empty() && !uav_barrier)
			{
				return;
			}
			suspend_rendering();

			buffer_barriers.clear();
			image_barriers.clear();
			for (const PendingTransition& t : pending_transitions)
			{
				const StateInfo before = get_state_info(t.before, shader_stages);
				const StateInfo after = get_state_info(t.after, shader_stages);
				if (t.texture)
				{
					image_barriers.push_back(vk::ImageMemoryBarrier2(before.stages, before.access, after.stages, after.access, before.layout, after.layout,
						VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, t.texture->get(), t.texture->get_full_range()));
				}
				else
				{
					buffer_barriers.push_back(vk::BufferMemoryBarrier2(before.stages, before.access, after.stages, after.access,
						VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, t.buffer->get(), 0, VK_WHOLE_SIZE));
				}
			}
			pending_transitions.clear();

			// UAV barrier equivalent: make shader storage writes visible to subsequent shader/indirect/transfer work
			const vk::MemoryBarrier2 memory_barrier(shader_stages, vk::AccessFlagBits2::eShaderStorageWrite,
				shader_stages | vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eTransferRead);
			if (uav_barrier)
			{
				uav_barrier_pending = false;
			}

			command_buffer.pipelineBarrier2(vk::DependencyInfo({}, uav_barrier ? 1u : 0u, &memory_barrier,
				static_cast<u32>(buffer_barriers.size()), buffer_barriers.data(), static_cast<u32>(image_barriers.size()), image_barriers.data()));
		}

		void CommandList::bind_shaders()
		{
			const Pipeline& p = *active_pipeline;
			command_buffer.bindShadersEXT(vk::ArrayProxy<const vk::ShaderStageFlagBits>(p.bound_stage_count, p.bound_stages.data()),
				vk::ArrayProxy<const vk::ShaderEXT>(p.bound_stage_count, p.bound_shaders.data()));
			if (p.get_bind_point() == vk::PipelineBindPoint::eGraphics)
			{
				if (!graphics_state_valid || !(current_graphics_state == p.graphics_state))
				{
					set_graphics_state(p.graphics_state);
				}
				vertex_input_dirty = true;
			}
			shaders_dirty = false;
		}

		void CommandList::set_graphics_state(const GraphicsState& state)
		{
			// With shader objects every piece of fixed function state a draw depends on is dynamic and must be set. State the
			// renderer never changes (no rasterizer discard, primitive restart, depth bias or bounds, single sample, unit line
			// width) is set here too, as binding a third party pipeline may have overwritten it.
			command_buffer.setRasterizerDiscardEnable(VK_FALSE);
			command_buffer.setPrimitiveTopology(state.topology);
			command_buffer.setPrimitiveRestartEnable(VK_FALSE);
			command_buffer.setPolygonModeEXT(state.polygon_mode);
			command_buffer.setCullMode(state.cull_mode);
			command_buffer.setFrontFace(state.front_face);
			command_buffer.setLineWidth(1.0f);
			if (device.supports_bresenham_lines())
			{
				command_buffer.setLineRasterizationModeEXT(vk::LineRasterizationMode::eBresenham); // D3D12's aliased lines (see Device)
			}
			command_buffer.setDepthBiasEnable(VK_FALSE);
			command_buffer.setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1);
			const vk::SampleMask sample_mask = 0xFFFFFFFFu;
			command_buffer.setSampleMaskEXT(vk::SampleCountFlagBits::e1, sample_mask);
			command_buffer.setAlphaToCoverageEnableEXT(state.alpha_to_coverage);

			command_buffer.setDepthTestEnable(state.depth_test);
			command_buffer.setDepthWriteEnable(state.depth_write);
			command_buffer.setDepthCompareOp(state.depth_compare);
			command_buffer.setDepthBoundsTestEnable(VK_FALSE);
			command_buffer.setStencilTestEnable(state.stencil_test);
			if (state.stencil_test)
			{
				const vk::StencilOpState& f = state.stencil_front;
				const vk::StencilOpState& b = state.stencil_back;
				command_buffer.setStencilOp(vk::StencilFaceFlagBits::eFront, f.failOp, f.passOp, f.depthFailOp, f.compareOp);
				command_buffer.setStencilOp(vk::StencilFaceFlagBits::eBack, b.failOp, b.passOp, b.depthFailOp, b.compareOp);
				command_buffer.setStencilCompareMask(vk::StencilFaceFlagBits::eFrontAndBack, f.compareMask);
				command_buffer.setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack, f.writeMask);
				command_buffer.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, 0); // D3D12's default OMSetStencilRef
			}

			if (device.supports_logic_ops())
			{
				command_buffer.setLogicOpEnableEXT(state.logic_op_enable);
				if (state.logic_op_enable) command_buffer.setLogicOpEXT(state.logic_op);
			}

			command_buffer.setColorBlendEnableEXT(0, state.blend_enables);
			command_buffer.setColorBlendEquationEXT(0, state.blend_equations);
			command_buffer.setColorWriteMaskEXT(0, state.write_masks);
			command_buffer.setBlendConstants({ 1.0f, 1.0f, 1.0f, 1.0f }); // D3D12's default blend factor

			current_graphics_state = state;
			graphics_state_valid = true;
		}

		void CommandList::set_vertex_input()
		{
			// Only slot 0 is used (bind_vertex_buffer binds one vertex buffer, like the D3D12 backend)
			const Pipeline& p = *active_pipeline;
			std::array<vk::VertexInputAttributeDescription2EXT, InputLayout::Desc::MAX_ELEMENTS> attributes;
			u32 attribute_count = 0;
			vk::VertexInputBindingDescription2EXT binding(0, static_cast<u32>(vertex_buffer_stride), vk::VertexInputRate::eVertex, 1);
			for (const Pipeline::VertexAttribute& a : p.vertex_attributes)
			{
				binding.inputRate = a.rate; // Device::create_pipeline checks all elements share one rate
				attributes[attribute_count++] = vk::VertexInputAttributeDescription2EXT(a.location, 0, a.format, a.offset);
			}
			command_buffer.setVertexInputEXT(vk::ArrayProxy<const vk::VertexInputBindingDescription2EXT>(attribute_count > 0 ? 1u : 0u, &binding),
				vk::ArrayProxy<const vk::VertexInputAttributeDescription2EXT>(attribute_count, attributes.data()));
			vertex_input_dirty = false;
		}

		void CommandList::set_viewport_scissor()
		{
			command_buffer.setViewportWithCount(pass.viewport);
			command_buffer.setScissorWithCount(pass.scissor);
			viewport_dirty = false;
		}

		void CommandList::write_descriptor_tables()
		{
			// Writes the SRV and UAV tables the active shaders read into this command list's heap region. Texture descriptors
			// are copied from the textures' caches and buffer descriptors written in one batch; unset entries the shaders use
			// get stand-ins.
			const Pipeline& p = *active_pipeline;
			tables_binding_version = binding_version;
			tables_pipeline = active_pipeline;
			const u32 table_slots = p.srv_table_size + p.uav_table_size;
			if (table_slots == 0)
			{
				return;
			}
			seAssert(region_used_slots + table_slots <= Device::COMMAND_LIST_DESCRIPTOR_SLOTS, "Command list descriptor heap region exhausted");
			if (region_used_slots + table_slots > Device::COMMAND_LIST_DESCRIPTOR_SLOTS)
			{
				return;
			}
			const u32 first_slot = region_first_slot + region_used_slots;
			region_used_slots += table_slots;
			push_data.srv_table_slot = first_slot;
			push_data.uav_table_slot = first_slot + p.srv_table_size;
			push_data_dirty = true;

			const u32 slot_size = device.get_descriptor_slot_size();
			uint8_t* const heap = device.get_resource_heap().get_mapped_data();
			std::array<vk::DescriptorType, BindingShift::END> buffer_types;
			std::array<vk::DeviceAddressRangeEXT, BindingShift::END> buffer_ranges;
			std::array<void*, BindingShift::END> buffer_destinations;
			u32 buffer_count = 0;

			for (const Pipeline::TableEntry& entry : p.table_entries)
			{
				const bool is_srv = entry.binding < BindingShift::UAV;
				const u32 index = entry.binding - (is_srv ? BindingShift::SRV : BindingShift::UAV);
				const u32 slot = (is_srv ? push_data.srv_table_slot : push_data.uav_table_slot) + index;
				uint8_t* destination = heap + static_cast<size_t>(slot) * slot_size;

				Texture* texture = nullptr;
				Buffer* buffer = nullptr;
				u64 range = 0;
				TextureViewKey view_key;
				if (is_srv)
				{
					ShaderResourceView& srv = active_binding.srvs[index];
					if (srv.texture_resource)
					{
						texture = srv.texture_resource.get();
						view_key = texture->make_shader_resource_view_key(srv.top_mip, srv.mip_count);
					}
					buffer = srv.buffer_resource.get();
					range = srv.element_size * srv.element_count;
				}
				else
				{
					UnorderedAccessView& uav = active_binding.uavs[index];
					if (uav.texture_resource)
					{
						texture = uav.texture_resource.get();
						view_key = texture->make_unordered_access_view_key(uav.mip_level);
					}
					buffer = uav.buffer_resource.get();
					range = uav.get_size_bytes();
				}

				if (entry.type == vk::DescriptorType::eStorageBuffer)
				{
					seAssert(buffer || !texture, "Shader binding %u expects a buffer but a texture is bound", entry.binding);
					if (buffer)
					{
						buffer_types[buffer_count] = vk::DescriptorType::eStorageBuffer;
						const u64 size = (range > 0 && range < buffer->get_size_bytes()) ? range : buffer->get_size_bytes();
						buffer_ranges[buffer_count] = vk::DeviceAddressRangeEXT(buffer->get_device_address(), size);
						buffer_destinations[buffer_count] = destination;
						buffer_count++;
					}
					else
					{
						memcpy(destination, device.get_null_descriptor(vk::DescriptorType::eStorageBuffer), slot_size);
					}
				}
				else // Sampled or storage image
				{
					seAssert(texture || !buffer, "Shader binding %u expects a texture but a buffer is bound", entry.binding);
					const uint8_t* source = texture ? texture->get_descriptor(view_key, entry.type) : device.get_null_descriptor(entry.type);
					seAssert(source != nullptr, "No resource bound for shader binding %u", entry.binding);
					if (source) memcpy(destination, source, slot_size);
				}
			}

			if (buffer_count > 0)
			{
				device.write_buffer_descriptors(buffer_count, buffer_types.data(), buffer_ranges.data(), buffer_destinations.data());
			}
		}

		void CommandList::flush_shader_state()
		{
			if (!active_pipeline)
			{
				return;
			}
			const Pipeline& p = *active_pipeline;

			if (heaps_dirty)
			{
				command_buffer.bindResourceHeapEXT(device.get_resource_heap().get_bind_info());
				command_buffer.bindSamplerHeapEXT(device.get_sampler_heap().get_bind_info());
				heaps_dirty = false;
				push_data_dirty = true;
			}
			if (shaders_dirty)
			{
				bind_shaders();
			}
			if (p.get_bind_point() == vk::PipelineBindPoint::eGraphics)
			{
				if (vertex_input_dirty && p.has_vertex_shader)
				{
					set_vertex_input();
				}
				if (viewport_dirty && pass.active)
				{
					set_viewport_scissor();
				}
			}

			if (tables_binding_version != binding_version || tables_pipeline != active_pipeline)
			{
				write_descriptor_tables();
			}

			// Constant buffers are device addresses, with a stand-in for registers the shaders use but the application left unset
			for (u32 i = 0; i < PushData::MAX_CBVS; i++)
			{
				vk::DeviceAddress address = active_binding.cbvs[i].address;
				if (address == 0 && (p.used_cbv_mask & (1u << i)))
				{
					address = device.get_null_constant_buffer_address();
				}
				if (push_data.cbv_addresses[i] != address)
				{
					push_data.cbv_addresses[i] = address;
					push_data_dirty = true;
				}
			}

			if (push_data_dirty)
			{
				command_buffer.pushDataEXT(vk::PushDataInfoEXT(0, vk::HostAddressRangeConstEXT(&push_data, sizeof(push_data))));
				push_data_dirty = false;
			}
		}

		void CommandList::ensure_bindings_valid()
		{
			seAssert(active_pipeline, "trying to submit gpu work without an active pipeline");
			if (!active_pipeline)
			{
				return;
			}
			const BindingDesc& pipeline_binding = active_pipeline->get_binding_desc();
			seAssert(active_binding.cbv_binding_count >= pipeline_binding.cbv_binding_count, "Binding mismatch");
			seAssert(active_binding.srv_binding_count >= pipeline_binding.srv_binding_count, "Binding mismatch");
			seAssert(active_binding.uav_binding_count >= pipeline_binding.uav_binding_count, "Binding mismatch");
			(void)pipeline_binding;
		}

		void CommandList::mark_uavs_written()
		{
			for (u32 i = 0; i < active_binding.uav_binding_count; i++)
			{
				if (active_binding.uavs[i])
				{
					uav_barrier_pending = true;
					return;
				}
			}
		}

		void CommandList::transition(Buffer* buffer, ResourceState before, ResourceState after)
		{
			// Upload heap memory never needs barriers (host writes are made visible by the queue submission)
			if (before == after || buffer->get_memory_type() == MemoryType::Upload)
			{
				return;
			}
			for (auto it = pending_transitions.begin(); it != pending_transitions.end(); ++it)
			{
				if (it->buffer == buffer)
				{
					// Merge with the not yet flushed transition of the same resource; drop it if it round trips
					it->after = after;
					if (it->before == it->after) pending_transitions.erase(it);
					return;
				}
			}
			pending_transitions.push_back({ buffer, nullptr, before, after });
		}

		void CommandList::transition(Texture* texture, ResourceState before, ResourceState after)
		{
			if (before == after)
			{
				return;
			}
			for (auto it = pending_transitions.begin(); it != pending_transitions.end(); ++it)
			{
				if (it->texture == texture)
				{
					it->after = after;
					if (it->before == it->after) pending_transitions.erase(it);
					return;
				}
			}
			pending_transitions.push_back({ nullptr, texture, before, after });
		}

		ResourceState CommandList::get_current_state(Buffer* buffer) const
		{
			for (u32 i = 0; i < active_binding.uav_binding_count; i++)
			{
				if (active_binding.uavs[i].buffer_resource.get() == buffer)
				{
					return ResourceState::UnorderedAccess;
				}
			}
			return buffer->get_read_resource_state();
		}

		ResourceState CommandList::get_current_state(Texture* texture) const
		{
			for (u32 i = 0; i < active_binding.uav_binding_count; i++)
			{
				if (active_binding.uavs[i].texture_resource.get() == texture)
				{
					return ResourceState::UnorderedAccess;
				}
			}
			for (u32 i = 0; pass.active && i < pass.render_target_count; i++)
			{
				if (pass.render_targets[i]->texture_resource.get() == texture) return ResourceState::RenderTarget;
			}
			if (pass.active && pass.depth_stencil && pass.depth_stencil->texture_resource.get() == texture)
			{
				return ResourceState::DepthWrite;
			}
			return texture->get_read_resource_state();
		}

		void CommandList::retain(const SharedPtr<Buffer>& buffer)
		{
			if (buffer) retained_buffers.push_back(buffer);
		}

		void CommandList::retain(const SharedPtr<Texture>& texture)
		{
			if (texture) retained_textures.push_back(texture);
		}
	}
}
