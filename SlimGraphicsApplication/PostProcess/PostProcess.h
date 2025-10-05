#pragma once
#include "sgTypes.h"
#include <sgPlatformInclude.h>
#include "UploadHeap.h"
#include "Model.h"
#include "ShaderSharedStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "DebugDraw.h"
#include "sgPlatformForwardDeclare.h"

class PostProcess
{
public:
	PostProcess(sg::SharedPtr<sg::Device>& _device);

	void Update(HWND hwnd, const se::GameInput& input, float delta_time, float total_time, const Camera& camera, DebugDraw& debug_draw);
	bool Render(sg::CommandList& command_list, sg::ConstantBufferView& cbv_camera, sg::UnorderedAccessView out_texture, sg::ShaderResourceView in_colour_texture, sg::ShaderResourceView in_depth_texture, SimpleLinearConstantBuffer& cbuffer, sg::u32 width, sg::u32 height);
	void LoadPipeline();

	enum class DispatchMode : int
	{
		DM_4x4,
		DM_8x4,
		DM_8x8,
		DM_16x8,
		DM_16x16,
		Count
	} dispatch_mode = DispatchMode::DM_8x8;

	struct DispatchModeInfo { sg::u32 x; sg::u32 y; const char* shader; };
	static constexpr DispatchModeInfo kDispatchModeInfo[(size_t)DispatchMode::Count]
		= { 
			{4,4,"ShaderBin_Debug/PostProcessComputeShader_THREAD_COUNT_4_4.PC_DXC"},
			{8,4,"ShaderBin_Debug/PostProcessComputeShader_THREAD_COUNT_8_4.PC_DXC"},
			{8,8,"ShaderBin_Debug/PostProcessComputeShader_THREAD_COUNT_8_8.PC_DXC"},
			{16,8,"ShaderBin_Debug/PostProcessComputeShader_THREAD_COUNT_16_8.PC_DXC"},
			{16,16,"ShaderBin_Debug/PostProcessComputeShader_THREAD_COUNT_16_16.PC_DXC"},
	};
	const DispatchModeInfo& GetActiveDispatchInfo(DispatchMode mode) const { return kDispatchModeInfo[(size_t)mode]; }

	enum class PostProcessTechnique : int
	{
		SimplePassthrough,
	} post_process_technique = PostProcessTechnique::SimplePassthrough;

	bool post_process_output[4] = { true, true, true, true };

	bool enabled = true;
	sg::BindingDesc pipeline_binding_desc;
	sg::PipelineDesc::Compute pipeline_desc;
	sg::SharedPtr<sg::Device> device;
	sg::SharedPtr<sg::Pipeline> pipeline;
	ShaderStructs::PostProcessData constant_data = {};
};