#pragma once
#include "sgTypes.h"
#include <sgPlatformInclude.h>
#include "sgUploadHeap.h"
#include "Model.h"
#include "ShaderSharedStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "DebugDraw.h"
#include "sgPlatformForwardDeclare.h"

class MagnifyingGlass
{
public:
	MagnifyingGlass(sg::SharedPtr<sg::Device>& _device);

	void Update(HWND hwnd, const se::GameInput& input, float delta_time, float total_time, const Camera& camera, DebugDraw& debug_draw);
	void Render(sg::CommandList& command_list, sg::ConstantBufferView& cbv_camera, sg::ShaderResourceView read_texture, SimpleLinearConstantBuffer& cbuffer);

	bool active = false;
	sg::u32 target_size = 512;
	sg::SharedPtr<sg::Texture> render_texture;
	sg::SharedPtr<sg::RenderTargetView> render_target_view;
	sg::SharedPtr<sg::Device> device;
	sg::SharedPtr<sg::Pipeline> pipeline;
	ShaderStructs::MagnifyingGlassData constant_data = {};
};