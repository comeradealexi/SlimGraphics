#include "BitonicSort.h"
#include "sgUtils.h"
using namespace sg;

BitonicSort::BitonicSort(sg::SharedPtr<sg::Device>& _device)
{
	BindingDesc pipeline_binding_desc = {};
	PipelineDesc::Compute pipeline_desc = {};

	pipeline_desc.compute_shader = create_compute_shader(*_device, "ShaderBin_Debug\\BitonicSort_LocalTileSort.PC_DXC");
	pipeline_tile_sort = _device->create_pipeline(pipeline_desc, pipeline_binding_desc);

	pipeline_desc.compute_shader = create_compute_shader(*_device, "ShaderBin_Debug\\BitonicSort_Merge.PC_DXC");
	pipeline_merge = _device->create_pipeline(pipeline_desc, pipeline_binding_desc);
}

