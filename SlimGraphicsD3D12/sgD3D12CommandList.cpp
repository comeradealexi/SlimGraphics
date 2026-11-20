#include "pch.h"
#include "sgD3D12CommandList.h"
#include "sgD3D12Device.h"
#include "sgD3D12Pipeline.h"
#include "sgD3D12RenderTargetView.h"
#include "sgD3D12DepthStencilView.h"
#include "sgD3D12TypesTranslator.h"
#include "sgUtils.h"

namespace sg
{
	namespace D3D12
	{
		CommandList::CommandList(ComPtr<ID3D12GraphicsCommandList6>& in_command_list, ComPtr<ID3D12CommandAllocator>& in_command_allocator) :
			command_list(in_command_list),
			command_allocator(in_command_allocator)
		{

		}

		void CommandList::start_recording()
		{
			CHECKHR(command_allocator->Reset());
			CHECKHR(command_list->Reset(command_allocator.Get(), nullptr));
			descriptor_heap_index = 0;

			ID3D12DescriptorHeap* heaps[2] = { descriptor_heap.Get(), sg_device->get_sampler_descriptor_heap().Get() };
			command_list->SetDescriptorHeaps(2, heaps);
		}

		void CommandList::end_recording()
		{
			CHECKHR(command_list->Close());
		}

		void CommandList::set_pipeline(Pipeline* pipeline)
		{
			command_list->SetPipelineState(pipeline->pipeline.Get());
			if (pipeline->compute)
			{
				command_list->SetComputeRootSignature(pipeline->root_signature.Get());
			}
			else
			{
				command_list->SetGraphicsRootSignature(pipeline->root_signature.Get());
				command_list->IASetPrimitiveTopology(pipeline->topology);
			}

			active_pipeline = pipeline;
		}

		void CommandList::bind(Binding& bind, PipelineType type)
		{
			// Never write past the end of the per-command-list descriptor heap (asserts are compiled out in release).
			// Checked before any barriers are recorded so the tracked binding state stays consistent.
			const u32 descriptors_needed = bind.cbv_binding_count + bind.srv_binding_count + bind.uav_binding_count;
			seAssert(descriptor_heap_index + descriptors_needed <= descriptor_heap_maximum, "maximum descriptor bindings exceeded");
			if (descriptor_heap_index + descriptors_needed > descriptor_heap_maximum)
			{
				return;
			}

			// Transition old UAVs back to read state and new UAVs to write state.
			// Work on the set of unique resources rather than per-slot so that a resource which stays bound
			// (possibly in a different slot, or via several mip views) is never transitioned twice or from the wrong state.
			if (active_binding.uav_binding_count || bind.uav_binding_count)
			{
				auto contains = [](ID3D12Resource* const* list, u32 count, ID3D12Resource* resource) -> bool
					{
						for (u32 i = 0; i < count; i++)
						{
							if (list[i] == resource) return true;
						}
						return false;
					};

				ID3D12Resource* old_resources[Binding::MAX_UAVS] = {};
				D3D12_RESOURCE_STATES old_read_states[Binding::MAX_UAVS] = {};
				u32 old_count = 0;
				for (u32 i = 0; i < active_binding.uav_binding_count; i++)
				{
					ID3D12Resource* resource = active_binding.d3d12_uavs[i].get_d3d12_resource();
					if (resource && !contains(old_resources, old_count, resource))
					{
						old_read_states[old_count] = active_binding.d3d12_uavs[i].get_d3d12_read_resource_state();
						old_resources[old_count++] = resource;
					}
				}

				ID3D12Resource* new_resources[Binding::MAX_UAVS] = {};
				D3D12_RESOURCE_STATES new_read_states[Binding::MAX_UAVS] = {};
				u32 new_count = 0;
				for (u32 i = 0; i < bind.uav_binding_count; i++)
				{
					ID3D12Resource* resource = bind.d3d12_uavs[i].get_d3d12_resource();
					if (resource && !contains(new_resources, new_count, resource))
					{
						new_read_states[new_count] = bind.d3d12_uavs[i].get_d3d12_read_resource_state();
						new_resources[new_count++] = resource;
					}
				}

				CD3DX12_RESOURCE_BARRIER barriers[Binding::MAX_UAVS * 2];
				u32 barrier_count = 0;
				for (u32 i = 0; i < old_count; i++)
				{
					if (!contains(new_resources, new_count, old_resources[i]))
					{
						barriers[barrier_count++] = CD3DX12_RESOURCE_BARRIER::Transition(old_resources[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, old_read_states[i]);
					}
				}
				for (u32 i = 0; i < new_count; i++)
				{
					if (!contains(old_resources, old_count, new_resources[i]))
					{
						barriers[barrier_count++] = CD3DX12_RESOURCE_BARRIER::Transition(new_resources[i], new_read_states[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					}
				}

				if (barrier_count)
				{
					command_list->ResourceBarrier(barrier_count, barriers);
				}
			}

			//Binding contains the global list indices.
			//ID3D12Device::CopyDescriptorsSimple method (d3d12.h)
			u32 root_parameter_index = 0;

			if (bind.cbv_binding_count)
			{
				seAssert(descriptor_heap_index + bind.cbv_binding_count <= descriptor_heap_maximum, "maxium descriptor bindings exceeded");
				CD3DX12_GPU_DESCRIPTOR_HANDLE dest_gpu(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
				CD3DX12_CPU_DESCRIPTOR_HANDLE dest_cpu(descriptor_heap->GetCPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);

				for (u32 i = 0; i < bind.cbv_binding_count; i++)
				{
					device->CreateConstantBufferView(&bind.d3d12_cbvs[i].desc, dest_cpu);
					dest_cpu.Offset(descriptor_heap_increment);
				}
				
				if (type == PipelineType::Geometry)
				{
					command_list->SetGraphicsRootDescriptorTable(root_parameter_index, dest_gpu);
				}
				else
				{
					command_list->SetComputeRootDescriptorTable(root_parameter_index, dest_gpu);
				}
				root_parameter_index++;
				descriptor_heap_index += bind.cbv_binding_count;
			}

			if (bind.srv_binding_count)
			{
				seAssert(descriptor_heap_index + bind.srv_binding_count <= descriptor_heap_maximum, "maxium descriptor bindings exceeded");
				CD3DX12_GPU_DESCRIPTOR_HANDLE dest_gpu(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
				CD3DX12_CPU_DESCRIPTOR_HANDLE dest_cpu(descriptor_heap->GetCPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);


				for (u32 i = 0; i < bind.srv_binding_count; i++)
				{
					ShaderResourceView& srv = bind.d3d12_srvs[i];
					seAssert(srv.buffer_resource || srv.texture_resource, "SRV slot %u has no resource", i);
					ID3D12Resource* pResource = srv.buffer_resource ? srv.buffer_resource->get().Get() : (srv.texture_resource ? srv.texture_resource->get().Get() : nullptr);
					device->CreateShaderResourceView(pResource, &srv.desc, dest_cpu);
					dest_cpu.Offset(descriptor_heap_increment);
				}

				if (type == PipelineType::Geometry)
				{
					command_list->SetGraphicsRootDescriptorTable(root_parameter_index, dest_gpu);
				}
				else
				{
					command_list->SetComputeRootDescriptorTable(root_parameter_index, dest_gpu);
				}
				root_parameter_index++;
				descriptor_heap_index += bind.srv_binding_count;
			}

			if (bind.uav_binding_count)
			{
				seAssert(descriptor_heap_index + bind.uav_binding_count <= descriptor_heap_maximum, "maxium descriptor bindings exceeded");
				CD3DX12_GPU_DESCRIPTOR_HANDLE dest_gpu(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
				CD3DX12_CPU_DESCRIPTOR_HANDLE dest_cpu(descriptor_heap->GetCPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);

				for (u32 i = 0; i < bind.uav_binding_count; i++)
				{
					UnorderedAccessView& uav = bind.d3d12_uavs[i];
					if (uav.buffer_resource)
					{
						device->CreateUnorderedAccessView(uav.buffer_resource->get().Get(), nullptr, &uav.desc, dest_cpu);
					}
					else if (uav.texture_resource)
					{
						device->CreateUnorderedAccessView(uav.texture_resource->get().Get(), nullptr, &uav.desc, dest_cpu);
					}
					else
					{
						seAssert(false, "Must have a buffer or texture associated with uav!");
					}
					dest_cpu.Offset(descriptor_heap_increment);					
				}

				if (type == PipelineType::Geometry)
				{
					command_list->SetGraphicsRootDescriptorTable(root_parameter_index, dest_gpu);
				}
				else
				{
					command_list->SetComputeRootDescriptorTable(root_parameter_index, dest_gpu);
				}
				root_parameter_index++;
				descriptor_heap_index += bind.uav_binding_count;
			}

			if (bind.sampler_binding_count)
			{
				if (type == PipelineType::Geometry)
				{
					command_list->SetGraphicsRootDescriptorTable(root_parameter_index, sg_device->get_sampler_descriptor_heap()->GetGPUDescriptorHandleForHeapStart());
				}
				else
				{
					command_list->SetComputeRootDescriptorTable(root_parameter_index, sg_device->get_sampler_descriptor_heap()->GetGPUDescriptorHandleForHeapStart());
				}
				root_parameter_index++;
			}

			active_binding = bind;
		}

		void CommandList::draw_instanced(u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location)
		{
			ensure_bindings_valid();
			command_list->DrawInstanced(vertex_count_per_instance, instance_count, start_vertex_location, start_instance_location);
			flush_bound_uavs();
		}

		void CommandList::draw_indexed_instanced(u32 index_count_per_instance, u32 instance_count, u32 start_index_location, i32 base_vertex_location, u32 start_instance_location)
		{
			ensure_bindings_valid();
			command_list->DrawIndexedInstanced(index_count_per_instance, instance_count, start_index_location, base_vertex_location, start_instance_location);
			flush_bound_uavs();
		}

		void CommandList::start_geometry_pass(u32 render_target_count, SharedPtr<RenderTargetView>* render_targets, const Viewport& viewport, const ScissorRect scissor, SharedPtr<DepthStencilView> depth_stencil)
		{
			seAssert(render_target_count == 1, "Only 1 supported atm TODO"); //TODO DON'T FORGET VIEWPORTS AND SCISSORS

			const D3D12_VIEWPORT d3d_viewport = translate(viewport);
			const D3D12_RECT d3d_scissor = translate(scissor);

			active_geometry_pass.rtvs[0] = render_targets[0];
			active_geometry_pass.dsv = depth_stencil;
			
			// RTV Transition
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(render_targets[0]->texture_resource->get().Get(), render_targets[0]->texture_resource->get_read_resource_state(), D3D12_RESOURCE_STATE_RENDER_TARGET);
				command_list->ResourceBarrier(1, &barrier);
			}

			if (depth_stencil)
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(depth_stencil->texture_resource->get().Get(), depth_stencil->texture_resource->get_read_resource_state(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
				command_list->ResourceBarrier(1, &barrier);
			}

			command_list->OMSetRenderTargets(1, &render_targets[0]->rtv_handle, FALSE, depth_stencil ? &depth_stencil->dsv_handle : nullptr);
			command_list->RSSetViewports(1, &d3d_viewport);
			command_list->RSSetScissorRects(1, &d3d_scissor);
		}


		void CommandList::bind_vertex_buffer(VertexBufferView view)
		{
			unbind_vertex_buffer();
			command_list->IASetVertexBuffers(0, 1, &view.vbv);
		}

		void CommandList::bind_index_buffer(IndexBufferView view)
		{
			unbind_index_buffer();
			command_list->IASetIndexBuffer(&view.ibv);
		}


		void CommandList::unbind_vertex_buffer()
		{
			// No need to transition? Always assume read state.
		}

		void CommandList::unbind_index_buffer()
		{
			// No need to transition? Always assume read state.
		}

		void CommandList::end_geometry_pass()
		{
			unbind_vertex_buffer();
			unbind_index_buffer();

			// RTV Transition
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(active_geometry_pass.rtvs[0]->texture_resource->get().Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, active_geometry_pass.rtvs[0]->texture_resource->get_read_resource_state());
				command_list->ResourceBarrier(1, &barrier);
			}
		
			if (active_geometry_pass.dsv)
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(active_geometry_pass.dsv->texture_resource->get().Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, active_geometry_pass.dsv->texture_resource->get_read_resource_state());
				command_list->ResourceBarrier(1, &barrier);
			}

			//To allow bound resources to be transitioned back to a read state.
			Binding default_binding;
			bind(default_binding, PipelineType::Geometry);

			active_geometry_pass = {};
		}

		void CommandList::dispatch(u32 x /*= 1*/, u32 y /*= 1*/, u32 z /*= 1*/)
		{
			ensure_bindings_valid();
			command_list->Dispatch(x, y, z);
			flush_bound_uavs();
		}


		void CommandList::dispatch_mesh(u32 x /*= 1*/, u32 y /*= 1*/, u32 z /*= 1*/)
		{
			ensure_bindings_valid();
			command_list->DispatchMesh(x, y, z);
			flush_bound_uavs();
		}

		void CommandList::execute_indirect(IndirectArguments& indirect_argument)
		{
			ensure_bindings_valid();
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(indirect_argument.indirect_buffer->get().Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
				command_list->ResourceBarrier(1, &barrier);
			}
			command_list->ExecuteIndirect(indirect_argument.command_signature.Get(), 1, indirect_argument.indirect_buffer->get().Get(), 0, nullptr, 0);
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(indirect_argument.indirect_buffer->get().Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				command_list->ResourceBarrier(1, &barrier);
			}
			flush_bound_uavs();
		}

		D3D12_RESOURCE_STATES CommandList::get_current_state(Buffer* buffer)
		{
			// Resources stay in UNORDERED_ACCESS while they are part of the active binding; otherwise they rest in their read state.
			ID3D12Resource* resource = buffer->get().Get();
			for (u32 i = 0; i < active_binding.uav_binding_count; i++)
			{
				if (active_binding.d3d12_uavs[i].get_d3d12_resource() == resource)
				{
					return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
				}
			}
			return buffer->get_read_resource_state();
		}

		void CommandList::transition(Buffer* buffer, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
		{
			if (before != after)
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(buffer->get().Get(), before, after);
				command_list->ResourceBarrier(1, &barrier);
			}
		}

		void CommandList::clear_buffer_float(UnorderedAccessView& uav, ShaderResourceView& srv, float value)
		{
			// ClearUnorderedAccessViewFloat is not valid on a raw (R32_TYPELESS) view, so clear with the float's bit pattern instead.
			// For 32-bit elements this produces exactly the same memory contents.
			sg::u32 bits = 0;
			static_assert(sizeof(bits) == sizeof(value), "size mismatch");
			memcpy(&bits, &value, sizeof(bits));
			clear_buffer_uint(uav, srv, bits);
		}

		void CommandList::clear_buffer_uint(UnorderedAccessView& uav, ShaderResourceView& srv, sg::u32 value)
		{
			seAssert(uav.buffer_resource, "clear_buffer_uint requires a buffer UAV");
			ID3D12Resource* buffer_dx12 = uav.buffer_resource->get().Get();

			seAssert(descriptor_heap_index + 1 <= descriptor_heap_maximum, "maximum descriptor bindings exceeded");
			CD3DX12_GPU_DESCRIPTOR_HANDLE dest_gpu(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
			CD3DX12_CPU_DESCRIPTOR_HANDLE dest_cpu(descriptor_heap->GetCPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
			descriptor_heap_index++;
			device->CreateUnorderedAccessView(buffer_dx12, nullptr, &uav.desc_uint, dest_cpu);

			// The CPU handle must come from a non-shader-visible heap. It is only read during the ClearUnorderedAccessView call
			// so a single slot can be reused for every clear.
			CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_handle(non_shader_descriptor_heap->GetCPUDescriptorHandleForHeapStart(), 0, descriptor_heap_increment);
			device->CreateUnorderedAccessView(buffer_dx12, nullptr, &uav.desc_uint, cpu_handle);

			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.UAV.pResource = buffer_dx12;

			const UINT clear_values[4] = { value, value, value, value };

			const D3D12_RESOURCE_STATES current_state = get_current_state(uav.buffer_resource.get());
			transition(uav.buffer_resource.get(), current_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			command_list->ResourceBarrier(1, &barrier);
			command_list->ClearUnorderedAccessViewUint(dest_gpu, cpu_handle, buffer_dx12, clear_values, 0, nullptr);
			command_list->ResourceBarrier(1, &barrier);

			transition(uav.buffer_resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, current_state);
		}

		void CommandList::copy_buffer_to_buffer(Buffer* dest, Buffer* source)
		{
			seAssert(dest && source, "no valid buffers");
			if (!dest || !source)
			{
				return;
			}
			seAssert(dest->get_size_bytes() >= source->get_size_bytes(), "Dest too small for source size");
			const u64 copy_size = std::min<u64>(dest->get_size_bytes(), source->get_size_bytes());

			// Readback and upload heap resources must stay in their creation state (COPY_DEST / GENERIC_READ).
			const bool dest_is_readback = dest->get_memory_type() == MemoryType::Readback;
			const bool source_is_upload = source->get_memory_type() == MemoryType::Upload;

			const D3D12_RESOURCE_STATES state_dest = get_current_state(dest);
			const D3D12_RESOURCE_STATES state_source = get_current_state(source);

			if (!dest_is_readback) transition(dest, state_dest, D3D12_RESOURCE_STATE_COPY_DEST);
			if (!source_is_upload) transition(source, state_source, D3D12_RESOURCE_STATE_COPY_SOURCE);

			// CopyBufferRegion (unlike CopyResource) allows the destination to be larger than the source.
			command_list->CopyBufferRegion(dest->get().Get(), 0, source->get().Get(), 0, copy_size);

			if (!dest_is_readback) transition(dest, D3D12_RESOURCE_STATE_COPY_DEST, state_dest);
			if (!source_is_upload) transition(source, D3D12_RESOURCE_STATE_COPY_SOURCE, state_source);
		}


		void CommandList::copy_buffer_to_buffer(u32 size, Buffer* dest, u32 dest_offset, Buffer* source, u32 source_offset)
		{
			seAssert(dest && source, "no valid buffers");
			if (!dest || !source)
			{
				return;
			}
			seAssert(dest_offset <= dest->get_size_bytes() && (dest->get_size_bytes() - dest_offset) >= size, "Dest buffer not big enough\n");
			seAssert(source_offset <= source->get_size_bytes() && (source->get_size_bytes() - source_offset) >= size, "Source buffer not big enough\n");
			seAssert(source->get_type() == BufferType::Upload, "only upload supported. Need to add different barrier transition for other types (to copy src)");
			const D3D12_RESOURCE_STATES state_dest = get_current_state(dest);

			transition(dest, state_dest, D3D12_RESOURCE_STATE_COPY_DEST);
			command_list->CopyBufferRegion(dest->get().Get(), dest_offset, source->get().Get(), source_offset, size);
			transition(dest, D3D12_RESOURCE_STATE_COPY_DEST, state_dest);
		}


		void CommandList::copy_texture_to_texture(Texture& dest, Texture& source)
		{
			const D3D12_RESOURCE_STATES state_dest = get_current_state(&dest);
			const D3D12_RESOURCE_STATES state_source = get_current_state(&source);

			transition(&dest, state_dest, D3D12_RESOURCE_STATE_COPY_DEST);
			transition(&source, state_source, D3D12_RESOURCE_STATE_COPY_SOURCE);
			command_list->CopyResource(dest.get().Get(), source.get().Get());
			transition(&dest, D3D12_RESOURCE_STATE_COPY_DEST, state_dest);
			transition(&source, D3D12_RESOURCE_STATE_COPY_SOURCE, state_source);
		}

		void CommandList::copy_buffer_to_texture(u32 size, Texture* dest, Buffer* source, u32 source_offset, u32 texture_mip_index)
		{
			seAssert(dest && source, "no valid buffers");
			if (!dest || !source)
			{
				return;
			}
			seAssert(source_offset <= source->get_size_bytes() && (source->get_size_bytes() - source_offset) >= size, "Source buffer not big enough\n");
			seAssert(source->get_type() == BufferType::Upload, "only upload supported. Need to add different barrier transition for other types (to copy src)");
			seAssert((source_offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT) == 0, "Texture upload offset must be 512 byte aligned");
			const D3D12_RESOURCE_STATES state_dest = get_current_state(dest);

			//Barrier transition
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(dest->get().Get(), state_dest, D3D12_RESOURCE_STATE_COPY_DEST);
				command_list->ResourceBarrier(1, &barrier);
			}

			const CD3DX12_TEXTURE_COPY_LOCATION tcl_dest(dest->get().Get(), texture_mip_index);

			D3D12_PLACED_SUBRESOURCE_FOOTPRINT d3d12_subresource_footprint;
			UINT64 row_size_bytes, total_bytes;
			D3D12_RESOURCE_DESC dest_desc = dest->get()->GetDesc();
			device->GetCopyableFootprints(&dest_desc, texture_mip_index, 1, 0, &d3d12_subresource_footprint, nullptr, &row_size_bytes, &total_bytes);

			D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_src = {};
			footprint_src.Offset = source_offset;
			footprint_src.Footprint.Width = d3d12_subresource_footprint.Footprint.Width;
			footprint_src.Footprint.Height = d3d12_subresource_footprint.Footprint.Height;
			footprint_src.Footprint.Depth = d3d12_subresource_footprint.Footprint.Depth;
			footprint_src.Footprint.Format = d3d12_subresource_footprint.Footprint.Format;
			// Source data must be laid out with the footprint row pitch (tight row size aligned to D3D12_TEXTURE_DATA_PITCH_ALIGNMENT).
			// UploadHeap::upload_texture_subresource writes data in exactly this layout.
			footprint_src.Footprint.RowPitch = d3d12_subresource_footprint.Footprint.RowPitch;
			seAssert(total_bytes <= size, "Upload data smaller than the texture subresource footprint");

			const CD3DX12_TEXTURE_COPY_LOCATION tcl_src(source->get().Get(), footprint_src);

			command_list->CopyTextureRegion(&tcl_dest, 0, 0, 0, &tcl_src, nullptr);

			//Barrier transition
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(dest->get().Get(), D3D12_RESOURCE_STATE_COPY_DEST, state_dest);
				command_list->ResourceBarrier(1, &barrier);
			}
		}

		void CommandList::clear_render_target_view(SharedPtr<RenderTargetView>& rtv, float4 colour)
		{
			command_list->ClearRenderTargetView(rtv->rtv_handle, colour.data(), 0, nullptr);
		}

		void CommandList::clear_depth_stencil_view(SharedPtr<DepthStencilView>& dsv, bool clear_depth, bool clear_stencil, float depth_value /*= 1.0f*/, u8 stencil_value /*= 0*/ )
		{
			D3D12_CLEAR_FLAGS clear_flags = {};
			clear_flags |= clear_depth ? D3D12_CLEAR_FLAG_DEPTH : (D3D12_CLEAR_FLAGS)0;
			clear_flags |= clear_stencil ? D3D12_CLEAR_FLAG_STENCIL : (D3D12_CLEAR_FLAGS)0;

			const D3D12_RESOURCE_STATES state_dest = dsv->texture_resource->get_read_resource_state();

			//Barrier transition
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(dsv->texture_resource->get().Get(), state_dest, D3D12_RESOURCE_STATE_DEPTH_WRITE);
				command_list->ResourceBarrier(1, &barrier);
			}

			command_list->ClearDepthStencilView(dsv->dsv_handle, clear_flags, depth_value, stencil_value, 0, nullptr);

			//Barrier transition
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(dsv->texture_resource->get().Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, state_dest);
				command_list->ResourceBarrier(1, &barrier);
			}
		}

		void CommandList::flush_bound_uavs()
		{
			if (active_binding.uav_binding_count)
			{
				D3D12_RESOURCE_BARRIER barriers[Binding::MAX_UAVS];

				for (size_t i = 0; i < active_binding.uav_binding_count; i++)
				{
					barriers[i] = {};
					barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
					barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
					barriers[i].UAV.pResource = active_binding.d3d12_uavs[i].get_d3d12_resource();
				}

				command_list->ResourceBarrier(active_binding.uav_binding_count, barriers);
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
		}
	}
}
