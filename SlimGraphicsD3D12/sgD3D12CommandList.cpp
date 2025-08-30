#include "pch.h"
#include "sgD3D12CommandList.h"
#include "sgD3D12Device.h"
#include "sgD3D12Pipeline.h"
#include "sgD3D12RenderTargetView.h"
#include "sgD3D12DepthStencilView.h"
#include "sgD3D12TypesTranslator.h"

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
		}

		void CommandList::bind(Binding& bind, PipelineType type)
		{
			//Transition old UAVs back to read state and new UAVs to write state
			const u32 highest_bind_count = std::max<u32>(active_binding.uav_binding_count, bind.uav_binding_count);
			if (highest_bind_count)
			{
				u32 barrier_count_out = 0;
				u32 barrier_count_in = 0;
				CD3DX12_RESOURCE_BARRIER barriers_out[Binding::MAX_UAVS];
				CD3DX12_RESOURCE_BARRIER barriers_in[Binding::MAX_UAVS];

				for (u32 i = 0; i < highest_bind_count; i++)
				{
					if (active_binding.get_uavs(i) != bind.get_uavs(i))
					{
						if (active_binding.get_uavs(i) != Binding::INVALID_BINDING)
						{
							barriers_out[barrier_count_out] = CD3DX12_RESOURCE_BARRIER::Transition(active_binding.d3d12_uavs[i].buffer_resource->get().Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, active_binding.d3d12_uavs[i].buffer_resource->get_read_resource_state());
							barrier_count_out++;
						}

						if (bind.get_uavs(i) != Binding::INVALID_BINDING)
						{
							barriers_in[barrier_count_in] = CD3DX12_RESOURCE_BARRIER::Transition(bind.d3d12_uavs[i].buffer_resource->get().Get(), bind.d3d12_uavs[i].buffer_resource->get_read_resource_state(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
							barrier_count_in++;
						}
					}
				}

				if (barrier_count_out)
				{
					command_list->ResourceBarrier(barrier_count_out, barriers_out);
				}

				if (barrier_count_in)
				{
					command_list->ResourceBarrier(barrier_count_in, barriers_in);
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
					ID3D12Resource* pResource = srv.buffer_resource ? srv.buffer_resource->get().Get() : srv.texture_resource->get().Get();
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
					device->CreateUnorderedAccessView(uav.buffer_resource->get().Get(), nullptr, &uav.desc, dest_cpu);
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
				root_parameter_index++;
				seAssert(false, "todo");
			}

			bind.set_not_dirty();
			active_binding = bind;
		}

		void CommandList::draw_instanced(u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location)
		{
			command_list->DrawInstanced(vertex_count_per_instance, instance_count, start_vertex_location, start_instance_location);
			flush_bound_uavs();
		}

		void CommandList::draw_indexed_instanced(u32 index_count_per_instance, u32 instance_count, u32 start_index_location, i32 base_vertex_location, u32 start_instance_location)
		{
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
			command_list->Dispatch(x, y, z);
			flush_bound_uavs();
		}


		void CommandList::dispatch_mesh(u32 x /*= 1*/, u32 y /*= 1*/, u32 z /*= 1*/)
		{
			command_list->DispatchMesh(x, y, z);
			flush_bound_uavs();
		}

		void CommandList::clear_buffer_float(UnorderedAccessView& uav, ShaderResourceView& srv, float value)
		{			
			ID3D12Resource* buffer_dx12 = uav.buffer_resource->get().Get();

			seAssert(descriptor_heap_index + 1 <= descriptor_heap_maximum, "maxium descriptor bindings exceeded");
			CD3DX12_GPU_DESCRIPTOR_HANDLE dest_gpu(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
			CD3DX12_CPU_DESCRIPTOR_HANDLE dest_cpu(descriptor_heap->GetCPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
			descriptor_heap_index++;
			device->CreateUnorderedAccessView(uav.buffer_resource->get().Get(), nullptr, &uav.desc_uint, dest_cpu);

			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.UAV.pResource = buffer_dx12;

			const FLOAT clear_values[4] = { value, value, value, value };

			// Surely this is wrong, allocating, using then freeing instantly.... ? But this descriptor heap isn't bound on the command list anyway
			// https://learn.microsoft.com/en-gb/windows/win32/direct3d12/non-shader-visible-descriptor-heaps?redirectedfrom=MSDN
			CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_handle(non_shader_descriptor_heap->GetCPUDescriptorHandleForHeapStart(), 0, descriptor_heap_increment);
			device->CreateUnorderedAccessView(buffer_dx12, nullptr, &uav.desc_uint, cpu_handle);

			command_list->ResourceBarrier(1, &barrier);
			command_list->ClearUnorderedAccessViewFloat(dest_gpu, cpu_handle, buffer_dx12, clear_values, 0, nullptr);
			command_list->ResourceBarrier(1, &barrier);
		}


		void CommandList::clear_buffer_uint(UnorderedAccessView& uav, ShaderResourceView& srv, sg::u32 value)
		{
			ID3D12Resource* buffer_dx12 = uav.buffer_resource->get().Get();

			seAssert(descriptor_heap_index + 1 <= descriptor_heap_maximum, "maximum descriptor bindings exceeded");
			CD3DX12_GPU_DESCRIPTOR_HANDLE dest_gpu(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
			CD3DX12_CPU_DESCRIPTOR_HANDLE dest_cpu(descriptor_heap->GetCPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
			descriptor_heap_index++;
			device->CreateUnorderedAccessView(uav.buffer_resource->get().Get(), nullptr, &uav.desc_uint, dest_cpu);

			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.UAV.pResource = buffer_dx12;

			const UINT clear_values[4] = { value, value, value, value };

			// Surely this is wrong, allocating, using then freeing instantly.... ? But this descriptor heap isn't bound on the command list anyway
			// https://learn.microsoft.com/en-gb/windows/win32/direct3d12/non-shader-visible-descriptor-heaps?redirectedfrom=MSDN
			CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_handle(non_shader_descriptor_heap->GetCPUDescriptorHandleForHeapStart(), 0, descriptor_heap_increment);
			device->CreateUnorderedAccessView(buffer_dx12,nullptr,&uav.desc_uint, cpu_handle);

			command_list->ResourceBarrier(1, &barrier);
			command_list->ClearUnorderedAccessViewUint(dest_gpu, cpu_handle, buffer_dx12, clear_values, 0, nullptr);
			command_list->ResourceBarrier(1, &barrier);
		}

		void CommandList::copy_buffer_to_buffer(Buffer* dest, Buffer* source)
		{
			seAssert(dest->get_size_bytes() >= source->get_size_bytes(), "Dest too small for source size");
			seAssert(dest && source, "no valid buffers");
			
			// Readback type does not need to transiton
			const bool dest_is_readback = dest->get_memory_type() == MemoryType::Readback;

			const D3D12_RESOURCE_STATES state_dest = dest->get_read_resource_state();

			//Barrier transition
			if (!dest_is_readback)
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(dest->get().Get(), state_dest, D3D12_RESOURCE_STATE_COPY_DEST);
				command_list->ResourceBarrier(1, &barrier);
			}
			else
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(source->get().Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
				command_list->ResourceBarrier(1, &barrier);
			}
			
			command_list->CopyResource(dest->get().Get(), source->get().Get());	
			
			//Barrier transition
			if (!dest_is_readback)
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(dest->get().Get(), D3D12_RESOURCE_STATE_COPY_DEST, state_dest);
				command_list->ResourceBarrier(1, &barrier);
			}
			else
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(source->get().Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				command_list->ResourceBarrier(1, &barrier);
			}
		}


		void CommandList::copy_buffer_to_buffer(u32 size, Buffer* dest, u32 dest_offset, Buffer* source, u32 source_offset)
		{
			seAssert((dest->get_size_bytes() - dest_offset) >= size, "Dest buffer not big enough\n");
			seAssert((source->get_size_bytes() - source_offset) >= size, "Source buffer not big enough\n");
			seAssert(dest && source, "no valid buffers");
			seAssert(source->get_type() == BufferType::Upload, "only upload supported. Need to add different barrier transition for other types (to copy src)");
			const D3D12_RESOURCE_STATES state_dest = dest->get_read_resource_state();

			//Barrier transition
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(dest->get().Get(), state_dest, D3D12_RESOURCE_STATE_COPY_DEST);
				command_list->ResourceBarrier(1, &barrier);
			}
			
			command_list->CopyBufferRegion(dest->get().Get(), dest_offset, source->get().Get(), source_offset, size);

			//Barrier transition
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(dest->get().Get(), D3D12_RESOURCE_STATE_COPY_DEST, state_dest);
				command_list->ResourceBarrier(1, &barrier);
			}
		}


		void CommandList::copy_texture_to_texture(Texture& dest, Texture& source)
		{
			const D3D12_RESOURCE_STATES state_dest = dest.get_read_resource_state();
			const D3D12_RESOURCE_STATES state_source = source.get_read_resource_state();

			CD3DX12_RESOURCE_BARRIER barriers[2] = {
				CD3DX12_RESOURCE_BARRIER::Transition(dest.get().Get(), state_dest, D3D12_RESOURCE_STATE_COPY_DEST),
				CD3DX12_RESOURCE_BARRIER::Transition(source.get().Get(), state_source, D3D12_RESOURCE_STATE_COPY_SOURCE)
			};
			command_list->ResourceBarrier(2, barriers);
			{
				command_list->CopyResource(dest.get().Get(), source.get().Get());
			}
			barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(dest.get().Get(), D3D12_RESOURCE_STATE_COPY_DEST, state_dest);
			barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(source.get().Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, state_source);
			command_list->ResourceBarrier(2, barriers);
		}

		void CommandList::copy_buffer_to_texture(u32 size, Texture* dest, Buffer* source, u32 source_offset)
		{
			seAssert((dest->get_size_bytes()) >= size, "Dest buffer not big enough\n");
			seAssert((source->get_size_bytes() - source_offset) >= size, "Source buffer not big enough\n");
			seAssert(dest && source, "no valid buffers");
			seAssert(source->get_type() == BufferType::Upload, "only upload supported. Need to add different barrier transition for other types (to copy src)");
			const D3D12_RESOURCE_STATES state_dest = dest->get_read_resource_state();

			//Barrier transition
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(dest->get().Get(), state_dest, D3D12_RESOURCE_STATE_COPY_DEST);
				command_list->ResourceBarrier(1, &barrier);
			}

			const CD3DX12_TEXTURE_COPY_LOCATION tcl_dest(dest->get().Get(), 0);

			D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_src = {};
			footprint_src.Offset = source_offset;
			footprint_src.Footprint.Width = dest->resource_create_desc.width;
			footprint_src.Footprint.Height = dest->resource_create_desc.height;
			footprint_src.Footprint.Depth = dest->resource_create_desc.depth;
			footprint_src.Footprint.Format = dest->resource_create_desc.format;
			footprint_src.Footprint.RowPitch = (dest->resource_create_desc.width * sg_device->GetFormatBitsPerUnit(dest->resource_create_desc.format)) / 8;
			seAssert(footprint_src.Footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT == 0, "Row pitch must align to D3D12_TEXTURE_DATA_PITCH_ALIGNMENT");

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
					barriers[i].UAV.pResource = active_binding.d3d12_uavs[i].buffer_resource->get().Get();
				}

				command_list->ResourceBarrier(active_binding.uav_binding_count, barriers);
			}
		}
	}
}
