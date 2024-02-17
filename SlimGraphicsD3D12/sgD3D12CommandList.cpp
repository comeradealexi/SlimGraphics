#include "pch.h"
#include "sgD3D12CommandList.h"
#include "sgD3D12Device.h"

namespace sg
{
	namespace D3D12
	{
		CommandList::CommandList(ComPtr<ID3D12GraphicsCommandList6>& in_command_list, ComPtr<ID3D12CommandAllocator>& in_command_allocator) :
			command_list(in_command_list),
			command_allocator(in_command_allocator)
		{

		}

		void CommandList::startRecording()
		{
			CHECKHR(command_list->Reset(command_allocator.Get(), nullptr));
			descriptor_heap_index = 0;
		}

		void CommandList::endRecording()
		{
			CHECKHR(command_list->Close());
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

				ComPtr<ID3D12DescriptorHeap> global_heap = device->get_cbv_srv_uav_descriptor_heap();
				ComPtr<ID3D12Device> dx12_device = device->get_device();
				for (size_t i = 0; i < bind.cbv_binding_count; i++)
				{
					CD3DX12_CPU_DESCRIPTOR_HANDLE src_cpu(global_heap->GetCPUDescriptorHandleForHeapStart(), bind.get_cbvs(i), descriptor_heap_increment);
					dx12_device->CopyDescriptorsSimple(1, dest_cpu, src_cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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

				ComPtr<ID3D12DescriptorHeap> global_heap = device->get_cbv_srv_uav_descriptor_heap();
				ComPtr<ID3D12Device> dx12_device = device->get_device();
				for (size_t i = 0; i < bind.srv_binding_count; i++)
				{
					CD3DX12_CPU_DESCRIPTOR_HANDLE src_cpu(global_heap->GetCPUDescriptorHandleForHeapStart(), bind.get_srvs(i), descriptor_heap_increment);
					dx12_device->CopyDescriptorsSimple(1, dest_cpu, src_cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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

				ComPtr<ID3D12DescriptorHeap> global_heap = device->get_cbv_srv_uav_descriptor_heap();
				ComPtr<ID3D12Device> dx12_device = device->get_device();
				for (size_t i = 0; i < bind.uav_binding_count; i++)
				{
					CD3DX12_CPU_DESCRIPTOR_HANDLE src_cpu(global_heap->GetCPUDescriptorHandleForHeapStart(), bind.get_uavs(i), descriptor_heap_increment);
					dx12_device->CopyDescriptorsSimple(1, dest_cpu, src_cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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
	}
}
