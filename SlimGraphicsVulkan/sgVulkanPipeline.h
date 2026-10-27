#pragma once
#include "sgD3D12Include.h"
#include "sgTypes.h"

namespace sg
{
	namespace D3D12
	{
		class Pipeline
		{
			friend class Device;
			friend class CommandList;
		public:
			Pipeline(const PipelineDesc::Compute& pipeline_desc) : compute_pipeline_desc(pipeline_desc), compute(true) { }
			Pipeline(const PipelineDesc::Graphics& pipeline_desc) : graphics_pipeline_desc(pipeline_desc), compute(false) {}
			Pipeline(const PipelineDesc::Mesh& pipeline_desc) : mesh_pipeline_desc(pipeline_desc), compute(false) {}

		private:
			const PipelineDesc::Compute compute_pipeline_desc;
			const PipelineDesc::Graphics graphics_pipeline_desc;
			const PipelineDesc::Mesh mesh_pipeline_desc;
			const bool compute;
			D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			ComPtr<ID3D12RootSignature> root_signature;
			ComPtr<ID3D12PipelineState> pipeline;
		};
	}
}

