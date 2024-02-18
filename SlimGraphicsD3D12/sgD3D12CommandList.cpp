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
			CHECKHR(command_list->Reset(command_allocator.Get(), nullptr));
			descriptor_heap_index = 0;

			ID3D12DescriptorHeap* heaps[2] = { descriptor_heap.Get(), global_sampler_descriptor_heap.Get() };
			command_list->SetDescriptorHeaps(2, heaps);
		}

		void CommandList::end_recording()
		{
			CHECKHR(command_list->Close());
		}

		void CommandList::set_pipeline(Pipeline* pipeline)
		{
			command_list->SetPipelineState(pipeline->pipeline.Get());
			command_list->SetGraphicsRootSignature(pipeline->root_signature.Get());
			command_list->IASetPrimitiveTopology(pipeline->topology);
		}

		void CommandList::bind(Binding& bind)
		{
			//Binding contains the global list indices.

			//ID3D12Device::CopyDescriptorsSimple method (d3d12.h)

			if (bind.cbv_binding_count)
			{
				seAssert(descriptor_heap_index + bind.cbv_binding_count <= descriptor_heap_maximum, "maxium descriptor bindings exceeded");
				CD3DX12_GPU_DESCRIPTOR_HANDLE dest_gpu(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
				CD3DX12_CPU_DESCRIPTOR_HANDLE dest_cpu(descriptor_heap->GetCPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);

				ComPtr<ID3D12DescriptorHeap> global_heap = global_cbv_srv_uav_descriptor_heap;
				for (u32 i = 0; i < bind.cbv_binding_count; i++)
				{
					CD3DX12_CPU_DESCRIPTOR_HANDLE src_cpu(global_heap->GetCPUDescriptorHandleForHeapStart(), bind.get_cbvs(i), descriptor_heap_increment);
					device->CopyDescriptorsSimple(1, dest_cpu, src_cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					dest_cpu.Offset(descriptor_heap_increment);
				}
				
				command_list->SetGraphicsRootDescriptorTable(0, dest_gpu);
				descriptor_heap_index += bind.cbv_binding_count;
			}

			if (bind.srv_binding_count)
			{
				seAssert(descriptor_heap_index + bind.srv_binding_count <= descriptor_heap_maximum, "maxium descriptor bindings exceeded");
				CD3DX12_GPU_DESCRIPTOR_HANDLE dest_gpu(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
				CD3DX12_CPU_DESCRIPTOR_HANDLE dest_cpu(descriptor_heap->GetCPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);

				ComPtr<ID3D12DescriptorHeap> global_heap = global_cbv_srv_uav_descriptor_heap;

				for (u32 i = 0; i < bind.srv_binding_count; i++)
				{
					CD3DX12_CPU_DESCRIPTOR_HANDLE src_cpu(global_heap->GetCPUDescriptorHandleForHeapStart(), bind.get_srvs(i), descriptor_heap_increment);
					device->CopyDescriptorsSimple(1, dest_cpu, src_cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					dest_cpu.Offset(descriptor_heap_increment);
				}

				command_list->SetGraphicsRootDescriptorTable(1, dest_gpu);
				descriptor_heap_index += bind.srv_binding_count;
			}

			if (bind.uav_binding_count)
			{
				seAssert(descriptor_heap_index + bind.uav_binding_count <= descriptor_heap_maximum, "maxium descriptor bindings exceeded");
				CD3DX12_GPU_DESCRIPTOR_HANDLE dest_gpu(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
				CD3DX12_CPU_DESCRIPTOR_HANDLE dest_cpu(descriptor_heap->GetCPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);

				ComPtr<ID3D12DescriptorHeap> global_heap = global_cbv_srv_uav_descriptor_heap;
				for (u32 i = 0; i < bind.uav_binding_count; i++)
				{
					CD3DX12_CPU_DESCRIPTOR_HANDLE src_cpu(global_heap->GetCPUDescriptorHandleForHeapStart(), bind.get_uavs(i), descriptor_heap_increment);
					device->CopyDescriptorsSimple(1, dest_cpu, src_cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					dest_cpu.Offset(descriptor_heap_increment);
				}

				command_list->SetGraphicsRootDescriptorTable(2, dest_gpu);
				descriptor_heap_index += bind.uav_binding_count;
			}

			if (bind.sampler_binding_count)
			{
				seAssert(false, "todo");
			}

			bind.set_not_dirty();
		}

		void CommandList::draw_instanced(u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location)
		{
			command_list->DrawInstanced(vertex_count_per_instance, instance_count, start_vertex_location, start_instance_location);
		}

		void CommandList::draw_indexed_instanced(u32 index_count_per_instance, u32 instance_count, u32 start_index_location, i8 base_vertex_location, u32 start_instance_location)
		{
			command_list->DrawIndexedInstanced(index_count_per_instance, instance_count, start_index_location, base_vertex_location, start_instance_location);
		}

		void CommandList::start_render_pass(u32 render_target_count, RenderTargetView* render_targets, const Viewport& viewport, const ScissorRect scissor, bool rtv0_is_swap_chain, DepthStencilView* depth_stencil)
		{
			seAssert(render_target_count == 1, "Only 1 supported atm TODO"); //TODO DON'T FORGET VIEWPORTS AND SCISSORS
			
			const D3D12_VIEWPORT d3d_viewport = translate(viewport);
			const D3D12_RECT d3d_scissor = translate(scissor);

			active_render_pass.rtv0_is_swap_chain = rtv0_is_swap_chain;
			active_render_pass.rtvs[0] = render_targets[0];
			
			if (rtv0_is_swap_chain)
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(render_targets[0].texture_resource->get().Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
				command_list->ResourceBarrier(1, &barrier);
			}

			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(global_rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart(), render_targets->rtv, descriptor_increment_size_rtv);
			//CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
			if (depth_stencil)
			{
				seAssert(false, "TODO");
			}
			command_list->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
			command_list->RSSetViewports(1, &d3d_viewport);
			command_list->RSSetScissorRects(1, &d3d_scissor);
		}

		void CommandList::end_render_pass()
		{
			if (active_render_pass.rtv0_is_swap_chain)
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(active_render_pass.rtvs[0].texture_resource->get().Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
				command_list->ResourceBarrier(1, &barrier);
			}

			active_render_pass = {};
		}
	}
}
