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

			u32 bind_point_index(vk::PipelineBindPoint bind_point)
			{
				return bind_point == vk::PipelineBindPoint::eCompute ? 1u : 0u;
			}
		}

		CommandList::CommandList(Device& device_, u32 queue_family) : device(device_), shader_stages(device_.get_shader_stages())
		{
			const vk::raii::Device& vk_device = device.get_device();
			command_pool = vk::raii::CommandPool(vk_device, vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eTransient, queue_family));
			vk::raii::CommandBuffers buffers(vk_device, vk::CommandBufferAllocateInfo(*command_pool, vk::CommandBufferLevel::ePrimary, 1));
			command_buffer = std::move(buffers[0]);
		}

		void CommandList::start_recording()
		{
			// The previous submission of this command list has completed (the caller waited on its fence), so resetting the pool
			// and releasing the resources it referenced is safe.
			command_pool.reset();
			retained_buffers.clear();
			retained_textures.clear();

			active_pipeline = nullptr;
			active_binding = Binding();
			binding_version = 0;
			pushed_layout[0] = pushed_layout[1] = nullptr;
			pipeline_dirty = false;
			viewport_dirty = true;
			uav_barrier_pending = false;
			rendering = false;
			pass = PassState();
			pending_transitions.clear();
			statistics = ActiveStatistics();

			command_buffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
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

			command_buffer.end();
		}

		void CommandList::set_pipeline(Pipeline* pipeline)
		{
			seAssert(pipeline != nullptr && *pipeline->pipeline, "Invalid pipeline");
			command_buffer.bindPipeline(pipeline->get_bind_point(), *pipeline->pipeline);
			active_pipeline = pipeline;
			pipeline_dirty = false;
		}

		void CommandList::bind(Binding& bind, PipelineType type)
		{
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
			const vk::DeviceSize stride = view.stride;
			// Stride is dynamic state, as in D3D12 vertex buffer views
			command_buffer.bindVertexBuffers2(0, buffer, offset, size, stride);
		}

		void CommandList::bind_index_buffer(IndexBufferView view)
		{
			retain(view.buffer_resource);
			command_buffer.bindIndexBuffer(view.buffer_resource->get(), view.offset, view.index_type);
		}

		void CommandList::draw_instanced(u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location)
		{
			ensure_bindings_valid();
			update_statistics_mode(false);
			flush_barriers(!rendering);
			ensure_rendering();
			flush_descriptors();
			command_buffer.draw(vertex_count_per_instance, instance_count, start_vertex_location, start_instance_location);
			mark_uavs_written();
		}

		void CommandList::draw_indexed_instanced(u32 index_count_per_instance, u32 instance_count, u32 start_index_location, i32 base_vertex_location, u32 start_instance_location)
		{
			ensure_bindings_valid();
			update_statistics_mode(false);
			flush_barriers(!rendering);
			ensure_rendering();
			flush_descriptors();
			command_buffer.drawIndexed(index_count_per_instance, instance_count, start_index_location, base_vertex_location, start_instance_location);
			mark_uavs_written();
		}

		void CommandList::start_geometry_pass(u32 render_target_count, SharedPtr<RenderTargetView>* render_targets, const Viewport& viewport, const ScissorRect scissor, SharedPtr<DepthStencilView> depth_stencil)
		{
			seAssert(!pass.active, "Geometry passes cannot be nested");
			seAssert(render_target_count <= 8, "Too many render targets");
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
			ensure_bindings_valid();
			suspend_rendering();
			flush_barriers(true);
			flush_descriptors();
			command_buffer.dispatch(x, y, z);
			mark_uavs_written();
		}

		void CommandList::dispatch_mesh(u32 x, u32 y, u32 z)
		{
			ensure_bindings_valid();
			update_statistics_mode(true);
			flush_barriers(!rendering);
			ensure_rendering();
			flush_descriptors();
			command_buffer.drawMeshTasksEXT(x, y, z);
			mark_uavs_written();
		}

		void CommandList::execute_indirect(IndirectArguments& indirect_argument)
		{
			// Only used for indirect DispatchMesh (see Device::create_indirect_arguments)
			ensure_bindings_valid();
			update_statistics_mode(true);
			Buffer* buffer = indirect_argument.indirect_buffer.get();
			retain(indirect_argument.indirect_buffer);
			const ResourceState state = get_current_state(buffer);
			transition(buffer, state, ResourceState::IndirectArgument);
			flush_barriers(true);
			ensure_rendering();
			flush_descriptors();
			command_buffer.drawMeshTasksIndirectEXT(buffer->get(), 0, 1, sizeof(VkDrawMeshTasksIndirectCommandEXT));
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

		void CommandList::clear_buffer_uint(UnorderedAccessView& uav, ShaderResourceView& srv, sg::u32 value)
		{
			seAssert(uav.buffer_resource, "clear_buffer_uint requires a buffer UAV");
			Buffer* buffer = uav.buffer_resource.get();
			retain(uav.buffer_resource);

			const ResourceState state = get_current_state(buffer);
			transition(buffer, state, ResourceState::CopyDest);
			suspend_rendering();
			flush_barriers(true);

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
			suspend_rendering();
			flush_barriers(true);

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
			suspend_rendering();
			flush_barriers(true);

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
			suspend_rendering();
			flush_barriers(true);

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
			suspend_rendering();
			flush_barriers(true);

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
			suspend_rendering();
			flush_barriers(true);
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
			suspend_rendering();
			flush_barriers(true);
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

			std::array<vk::RenderingAttachmentInfo, 8> colour_attachments;
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

			if (viewport_dirty)
			{
				command_buffer.setViewport(0, pass.viewport);
				command_buffer.setScissor(0, pass.scissor);
				viewport_dirty = false;
			}
		}

		void CommandList::invalidate_state()
		{
			pushed_layout[0] = pushed_layout[1] = nullptr;
			pipeline_dirty = active_pipeline != nullptr;
			viewport_dirty = true;
			if (rendering && pass.active)
			{
				command_buffer.setViewport(0, pass.viewport);
				command_buffer.setScissor(0, pass.scissor);
				viewport_dirty = false;
			}
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

			std::vector<vk::BufferMemoryBarrier2> buffer_barriers;
			std::vector<vk::ImageMemoryBarrier2> image_barriers;
			buffer_barriers.reserve(pending_transitions.size());
			image_barriers.reserve(pending_transitions.size());
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
			vk::MemoryBarrier2 memory_barrier(shader_stages, vk::AccessFlagBits2::eShaderStorageWrite,
				shader_stages | vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eTransferRead);
			if (uav_barrier)
			{
				uav_barrier_pending = false;
			}

			command_buffer.pipelineBarrier2(vk::DependencyInfo({}, uav_barrier ? 1u : 0u, &memory_barrier,
				static_cast<u32>(buffer_barriers.size()), buffer_barriers.data(), static_cast<u32>(image_barriers.size()), image_barriers.data()));
		}

		void CommandList::flush_descriptors()
		{
			if (!active_pipeline)
			{
				return;
			}
			if (pipeline_dirty)
			{
				command_buffer.bindPipeline(active_pipeline->get_bind_point(), *active_pipeline->pipeline);
				pipeline_dirty = false;
			}

			const PipelineLayout* layout = active_pipeline->layout.get();
			const u32 bp = bind_point_index(active_pipeline->get_bind_point());
			if (pushed_layout[bp] == layout && pushed_version[bp] == binding_version)
			{
				return; // Pushed descriptors are still valid (same layout object, same binding)
			}
			pushed_layout[bp] = layout;
			pushed_version[bp] = binding_version;
			if (layout->bindings.empty())
			{
				return;
			}

			std::array<vk::DescriptorImageInfo, BindingShift::END> image_infos;
			std::array<vk::DescriptorBufferInfo, BindingShift::END> buffer_infos;
			std::array<vk::WriteDescriptorSet, BindingShift::END> writes;
			u32 write_count = 0;
			const bool null_descriptors = device.supports_null_descriptors();

			for (const PipelineLayout::Binding& b : layout->bindings)
			{
				vk::DescriptorImageInfo& image_info = image_infos[write_count];
				vk::DescriptorBufferInfo& buffer_info = buffer_infos[write_count];
				image_info = vk::DescriptorImageInfo();
				buffer_info = vk::DescriptorBufferInfo(VK_NULL_HANDLE, 0, VK_WHOLE_SIZE);
				bool valid = false;

				if (b.binding < BindingShift::SRV)
				{
					const ConstantBufferView& cbv = active_binding.cbvs[b.binding - BindingShift::CBV];
					if (cbv.buffer)
					{
						buffer_info = vk::DescriptorBufferInfo(cbv.buffer, cbv.offset, cbv.size);
						valid = true;
					}
				}
				else if (b.binding < BindingShift::SAMPLER)
				{
					ShaderResourceView& srv = active_binding.srvs[b.binding - BindingShift::SRV];
					if (srv.texture_resource)
					{
						image_info = vk::DescriptorImageInfo(VK_NULL_HANDLE, srv.texture_resource->get_shader_resource_view(srv.top_mip, srv.mip_count), vk::ImageLayout::eReadOnlyOptimal);
						valid = true;
					}
					else if (srv.buffer_resource)
					{
						const u64 range = srv.element_size * srv.element_count;
						buffer_info = vk::DescriptorBufferInfo(srv.buffer_resource->get(), 0, range ? range : VK_WHOLE_SIZE);
						valid = true;
					}
				}
				else if (b.binding < BindingShift::UAV)
				{
					const vk::Sampler sampler = device.get_sampler(b.binding - BindingShift::SAMPLER);
					image_info = vk::DescriptorImageInfo(sampler, VK_NULL_HANDLE, vk::ImageLayout::eUndefined);
					valid = sampler != VK_NULL_HANDLE;
				}
				else
				{
					UnorderedAccessView& uav = active_binding.uavs[b.binding - BindingShift::UAV];
					if (uav.texture_resource)
					{
						image_info = vk::DescriptorImageInfo(VK_NULL_HANDLE, uav.texture_resource->get_unordered_access_view(uav.mip_level), vk::ImageLayout::eGeneral);
						valid = true;
					}
					else if (uav.buffer_resource)
					{
						const u64 range = uav.get_size_bytes();
						buffer_info = vk::DescriptorBufferInfo(uav.buffer_resource->get(), 0, range ? range : VK_WHOLE_SIZE);
						valid = true;
					}
				}

				if (!valid)
				{
					// D3D12 tolerates unset descriptors the shader never reads; with nullDescriptor Vulkan can match that
					seAssert(null_descriptors, "No resource bound for shader binding %u", b.binding);
					if (!null_descriptors)
					{
						continue;
					}
				}

				const bool is_image = b.type == vk::DescriptorType::eSampledImage || b.type == vk::DescriptorType::eStorageImage || b.type == vk::DescriptorType::eSampler;
				writes[write_count] = vk::WriteDescriptorSet(VK_NULL_HANDLE, b.binding, 0, 1, b.type, is_image ? &image_info : nullptr, is_image ? nullptr : &buffer_info);
				write_count++;
			}

			command_buffer.pushDescriptorSetKHR(active_pipeline->get_bind_point(), *layout->layout, 0, vk::ArrayProxy<const vk::WriteDescriptorSet>(write_count, writes.data()));
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
