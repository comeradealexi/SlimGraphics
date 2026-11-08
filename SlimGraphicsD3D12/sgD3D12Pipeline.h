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
			Pipeline(const PipelineDesc::Compute& pipeline_desc, const BindingDesc& pipeline_binding_desc) 
				: compute_pipeline_desc(pipeline_desc), compute(true), binding_desc(pipeline_binding_desc) { }
			Pipeline(const PipelineDesc::Graphics& pipeline_desc, const BindingDesc& pipeline_binding_desc) 
				: graphics_pipeline_desc(pipeline_desc), compute(false), binding_desc(pipeline_binding_desc) {}
			Pipeline(const PipelineDesc::Mesh& pipeline_desc, const BindingDesc& pipeline_binding_desc) 
				: mesh_pipeline_desc(pipeline_desc), compute(false), binding_desc(pipeline_binding_desc) {}

			const BindingDesc& get_binding_desc() const { return binding_desc; }
		private:
			const BindingDesc binding_desc;
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

