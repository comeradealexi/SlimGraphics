#include "gdrSort.h"
#include "sgUtils.h"
#include <Utils.h>

using namespace sg;

namespace gdr
{
	GPUSort::GPUSort(sg::SharedPtr<sg::Device>& device)
	{
		BindingDesc pipeline_binding_desc = {};
		pipeline_binding_desc.uav_binding_count = 2;
		pipeline_binding_desc.cbv_binding_count = 1;

		PipelineDesc::Compute desc;

		desc.compute_shader = create_compute_shader(*device, get_shader_path("GDR_Sort_LocalTileSortCS"));
		pipelines[SortDirection::Ascending].pipeline_local = device->create_pipeline(desc, pipeline_binding_desc);
		seAssert(pipelines[SortDirection::Ascending].pipeline_local != nullptr, "Failed to create pipeline");

		desc.compute_shader = create_compute_shader(*device, get_shader_path("GDR_Sort_LocalTileSortCS_DESCENDING"));
		pipelines[SortDirection::Descending].pipeline_local = device->create_pipeline(desc, pipeline_binding_desc);
		seAssert(pipelines[SortDirection::Descending].pipeline_local != nullptr, "Failed to create pipeline");

		desc.compute_shader = create_compute_shader(*device, get_shader_path("GDR_Sort_GlobalMergeCS"));
		pipelines[SortDirection::Ascending].pipeline_global = device->create_pipeline(desc, pipeline_binding_desc);
		seAssert(pipelines[SortDirection::Ascending].pipeline_global != nullptr, "Failed to create pipeline");

		desc.compute_shader = create_compute_shader(*device, get_shader_path("GDR_Sort_GlobalMergeCS_DESCENDING"));
		pipelines[SortDirection::Descending].pipeline_global = device->create_pipeline(desc, pipeline_binding_desc);
		seAssert(pipelines[SortDirection::Descending].pipeline_global != nullptr, "Failed to create pipeline");
	}

	void GPUSort::sort(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer, sg::UnorderedAccessView& array_to_sort, sg::UnorderedAccessView& array_size, u32 maximum_elements, SortDirection sort_direction)
	{
		struct GlobalMergeParams
		{
			uint32_t k;
			uint32_t j;
			uint32_t dummy0;
			uint32_t dummy1;
		} global_merge_params = {};

		Binding b = {};
		b.uav_binding_count = 2;
		b.cbv_binding_count = 1;

		b.set_uav(array_to_sort, 0);
		b.set_uav(array_size, 1);

		static constexpr uint32_t TILE_SIZE = 32;

		// Local Sort
		{
			command_list.set_pipeline(pipelines[sort_direction].pipeline_local.get());

			ConstantBufferView cbv0 = cbuffer.AllocateAndWrite(global_merge_params);
			b.set_cbv(cbv0, 0);

			command_list.bind(b, PipelineType::Compute);
			command_list.dispatch(maximum_elements / TILE_SIZE, 1, 1);
		}

		// Global Merge
		{
			command_list.set_pipeline(pipelines[sort_direction].pipeline_global.get());
			// 2) Phase B: Hierarchical global merges
			// Start k from 2 * TILE_SIZE (we already did k <= TILE_SIZE inside local tiles)
			for (u32 k = 2u * TILE_SIZE; k <= maximum_elements; k <<= 1u)
			{
				for (u32 j = k >> 1u; j >= 1; j >>= 1u)
				{
					global_merge_params = { k, j, 0, 0 };
					ConstantBufferView cbv0 = cbuffer.AllocateAndWrite(global_merge_params);
					cbv0 = cbuffer.AllocateAndWrite(global_merge_params);
					b.set_cbv(cbv0, 0);

					command_list.bind(b, PipelineType::Compute);

					// Dispatch one thread per element
					uint32_t groups = (maximum_elements + TILE_SIZE - 1u) / TILE_SIZE;
					command_list.dispatch(groups / 2, 1, 1);
				}
			}
		}

		//Binding default_binding; // TODO - Really shouldn't have to do this but otherwise resource dangles bound as a UAV...
		//command_list.bind(default_binding, PipelineType::Compute);
		//command_list.bind(default_binding, PipelineType::Geometry);
	}
}