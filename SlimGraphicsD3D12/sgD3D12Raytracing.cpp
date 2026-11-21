#include "pch.h"
#include "sgD3D12Device.h"
#include "sgD3D12CommandList.h"
#include "sgD3D12Buffer.h"
#include "sgD3D12Texture.h"
#include "sgD3D12ShaderResourceView.h"
#include "sgD3D12UnorderedAccessView.h"
#include "sgD3D12TypesTranslator.h"
#include "sgUtils.h"

// Hardware ray tracing (acceleration structures for inline ray queries), bindless descriptors and texture clears.
// Kept apart from sgD3D12Device.cpp / sgD3D12CommandList.cpp, which predate them.

namespace sg
{
	namespace D3D12
	{
		namespace
		{
			std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> translate_geometries(const Raytracing::GeometryDesc* geometries, u32 geometry_count)
			{
				std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> out(geometry_count);
				for (u32 i = 0; i < geometry_count; i++)
				{
					const Raytracing::GeometryDesc& g = geometries[i];
					D3D12_RAYTRACING_GEOMETRY_DESC& d = out[i];
					d = {};
					d.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
					d.Flags = g.opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
					d.Triangles.VertexFormat = g.vertex_format;
					d.Triangles.VertexCount = g.vertex_count;
					d.Triangles.VertexBuffer.StrideInBytes = g.vertex_stride;
					// Address queries only: works for buffers without the descriptor (prebuild info passes null buffers)
					d.Triangles.VertexBuffer.StartAddress = g.vertex_buffer ? g.vertex_buffer->get()->GetGPUVirtualAddress() + g.vertex_offset : 0;
					d.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
					d.Triangles.IndexCount = g.index_count;
					d.Triangles.IndexBuffer = g.index_buffer ? g.index_buffer->get()->GetGPUVirtualAddress() + g.index_offset : 0;
				}
				return out;
			}

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom_level_inputs(const std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& geometries)
			{
				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
				inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
				inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
				inputs.NumDescs = static_cast<UINT>(geometries.size());
				inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
				inputs.pGeometryDescs = geometries.data();
				return inputs;
			}

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top_level_inputs(u32 instance_count, D3D12_GPU_VIRTUAL_ADDRESS instances)
			{
				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
				inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
				inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
				inputs.NumDescs = instance_count;
				inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
				inputs.InstanceDescs = instances;
				return inputs;
			}

			Raytracing::PrebuildInfo prebuild_info(ID3D12Device5* device5, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs)
			{
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
				device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
				Raytracing::PrebuildInfo out;
				out.result_size = AlignUp<u64>(info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
				out.scratch_size = AlignUp<u64>(std::max<u64>(info.ScratchDataSizeInBytes, 1), D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
				return out;
			}
		}

		ID3D12Resource* ShaderResourceView::get_view_resource() const
		{
			if (acceleration_structure)
			{
				return nullptr;
			}
			if (buffer_resource)
			{
				return buffer_resource->get().Get();
			}
			return texture_resource ? texture_resource->get().Get() : nullptr;
		}

		bool Device::supports_raytracing()
		{
			// Inline ray queries (RayQuery in any shader stage) need tier 1.1
			return features.RaytracingTier() >= D3D12_RAYTRACING_TIER_1_1;
		}

		Raytracing::PrebuildInfo Device::get_bottom_level_prebuild_info(const Raytracing::GeometryDesc* geometries, u32 geometry_count)
		{
			if (!device5)
			{
				CHECKHR(device.As(&device5));
			}
			const std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> d3d12_geometries = translate_geometries(geometries, geometry_count);
			return prebuild_info(device5.Get(), bottom_level_inputs(d3d12_geometries));
		}

		Raytracing::PrebuildInfo Device::get_top_level_prebuild_info(u32 instance_count)
		{
			if (!device5)
			{
				CHECKHR(device.As(&device5));
			}
			return prebuild_info(device5.Get(), top_level_inputs(instance_count, 0));
		}

		u64 Device::get_gpu_address(Buffer* buffer)
		{
			seAssert(buffer != nullptr, "Invalid buffer");
			return buffer->get()->GetGPUVirtualAddress();
		}

		ShaderResourceView Device::create_acceleration_structure_view(SharedPtr<Buffer> buffer)
		{
			seAssert(buffer && buffer->get_type() == BufferType::AccelerationStructure, "Expected an acceleration structure buffer");
			ShaderResourceView srv;
			srv.buffer_resource = buffer;
			srv.acceleration_structure = true;
			srv.desc = {};
			srv.desc.Format = DXGI_FORMAT_UNKNOWN;
			srv.desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			srv.desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv.desc.RaytracingAccelerationStructure.Location = buffer->get()->GetGPUVirtualAddress();
			srv.desc_uint = srv.desc;
			return srv;
		}

		u32 Device::allocate_bindless_descriptor(ShaderResourceView& srv)
		{
			if (!bindless_descriptor_heap)
			{
				D3D12_DESCRIPTOR_HEAP_DESC desc = {};
				desc.NumDescriptors = BINDLESS_DESCRIPTOR_COUNT;
				desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				ComPtr<ID3D12DescriptorHeap> heap;
				CHECKHR(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)));
				heap->SetName(L"Device Bindless CBV SRV UAV Heap");
				bindless_descriptor_heap = SharedPtr<DescriptorHeap>(new DescriptorHeap(heap, desc.NumDescriptors, device->GetDescriptorHandleIncrementSize(desc.Type)));
				bindless_resources.resize(BINDLESS_DESCRIPTOR_COUNT);
			}

			const u32 index = bindless_descriptor_heap->allocate();
			seAssert(index != DescriptorHeap::INVALID_HEAP_INDEX && index < BINDLESS_DESCRIPTOR_COUNT, "Out of bindless descriptors");
			if (index == DescriptorHeap::INVALID_HEAP_INDEX || index >= BINDLESS_DESCRIPTOR_COUNT)
			{
				return INVALID_BINDLESS_DESCRIPTOR;
			}
			device->CreateShaderResourceView(srv.get_view_resource(), &srv.desc, bindless_descriptor_heap->get_cpu_handle_at_offset(index));
			bindless_resources[index] = srv.buffer_resource ? srv.buffer_resource : std::static_pointer_cast<Buffer>(srv.texture_resource);
			bindless_high_water = std::max(bindless_high_water, index + 1);
			bindless_version++;
			return index;
		}

		void Device::free_bindless_descriptor(u32 index)
		{
			if (!bindless_descriptor_heap || index >= BINDLESS_DESCRIPTOR_COUNT)
			{
				return;
			}
			// A null descriptor, so the slot never refers to a released resource
			D3D12_SHADER_RESOURCE_VIEW_DESC null_desc = {};
			null_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			null_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			null_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			null_desc.Texture2D.MipLevels = 1;
			device->CreateShaderResourceView(nullptr, &null_desc, bindless_descriptor_heap->get_cpu_handle_at_offset(index));
			bindless_resources[index] = nullptr;
			bindless_descriptor_heap->free(index);
			bindless_version++;
		}

		void CommandList::build_bottom_level_acceleration_structure(const Raytracing::GeometryDesc* geometries, u32 geometry_count, Buffer* dest, Buffer* scratch, u64 dest_offset)
		{
			seAssert(dest && dest->get_type() == BufferType::AccelerationStructure, "Destination must be an acceleration structure buffer");
			seAssert(scratch && scratch->get_type() == BufferType::GeneralDataBuffer, "Scratch must be a general data buffer with UAV access");
			const std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> d3d12_geometries = translate_geometries(geometries, geometry_count);

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
			desc.Inputs = bottom_level_inputs(d3d12_geometries);
			seAssert(dest_offset % D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT == 0, "Acceleration structures start at 256 byte boundaries");
			desc.DestAccelerationStructureData = dest->get()->GetGPUVirtualAddress() + dest_offset;
			desc.ScratchAccelerationStructureData = scratch->get()->GetGPUVirtualAddress();

			const D3D12_RESOURCE_STATES scratch_state = get_current_state(scratch);
			transition(scratch, scratch_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			command_list->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
			const CD3DX12_RESOURCE_BARRIER barriers[2] = { CD3DX12_RESOURCE_BARRIER::UAV(dest->get().Get()), CD3DX12_RESOURCE_BARRIER::UAV(scratch->get().Get()) };
			command_list->ResourceBarrier(2, barriers);
			transition(scratch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, scratch_state);
		}

		void CommandList::build_top_level_acceleration_structure(Buffer* instances, u32 instance_count, Buffer* dest, Buffer* scratch)
		{
			seAssert(dest && dest->get_type() == BufferType::AccelerationStructure, "Destination must be an acceleration structure buffer");
			seAssert(scratch && scratch->get_type() == BufferType::GeneralDataBuffer, "Scratch must be a general data buffer with UAV access");
			seAssert(instances != nullptr, "No instance buffer");

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
			desc.Inputs = top_level_inputs(instance_count, instances->get()->GetGPUVirtualAddress());
			desc.DestAccelerationStructureData = dest->get()->GetGPUVirtualAddress();
			desc.ScratchAccelerationStructureData = scratch->get()->GetGPUVirtualAddress();

			const D3D12_RESOURCE_STATES scratch_state = get_current_state(scratch);
			transition(scratch, scratch_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			command_list->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
			const CD3DX12_RESOURCE_BARRIER barriers[2] = { CD3DX12_RESOURCE_BARRIER::UAV(dest->get().Get()), CD3DX12_RESOURCE_BARRIER::UAV(scratch->get().Get()) };
			command_list->ResourceBarrier(2, barriers);
			transition(scratch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, scratch_state);
		}

		void CommandList::clear_texture_float(UnorderedAccessView& uav, float4 value)
		{
			seAssert(uav.texture_resource, "clear_texture_float requires a texture UAV");
			ID3D12Resource* resource = uav.texture_resource->get().Get();

			seAssert(descriptor_heap_index + 1 <= descriptor_heap_maximum, "maximum descriptor bindings exceeded");
			CD3DX12_GPU_DESCRIPTOR_HANDLE dest_gpu(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
			CD3DX12_CPU_DESCRIPTOR_HANDLE dest_cpu(descriptor_heap->GetCPUDescriptorHandleForHeapStart(), descriptor_heap_index, descriptor_heap_increment);
			descriptor_heap_index++;
			device->CreateUnorderedAccessView(resource, nullptr, &uav.desc, dest_cpu);
			// Only read during the clear, so the one non shader visible slot is reused (as clear_buffer_uint does)
			CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_handle(non_shader_descriptor_heap->GetCPUDescriptorHandleForHeapStart(), 0, descriptor_heap_increment);
			device->CreateUnorderedAccessView(resource, nullptr, &uav.desc, cpu_handle);

			const D3D12_RESOURCE_STATES current_state = get_current_state(uav.texture_resource.get());
			transition(uav.texture_resource.get(), current_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			const CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(resource);
			command_list->ResourceBarrier(1, &barrier);
			command_list->ClearUnorderedAccessViewFloat(dest_gpu, cpu_handle, resource, value.data(), 0, nullptr);
			command_list->ResourceBarrier(1, &barrier);
			transition(uav.texture_resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, current_state);
		}
	}
}
