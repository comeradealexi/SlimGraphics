#pragma once
#include "sgTypes.h"
#include <sgPlatformInclude.h>
#include "UploadHeap.h"
#include "Model.h"
#include "ShaderSharedStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "DebugDraw.h"

class MagnifyingGlass
{
public:
	MagnifyingGlass(sg::SharedPtr<sg::Device>& _device);

	void Update(float delta_time, float total_time, const Camera& camera, DebugDraw& debug_draw);
	void Render(sg::CommandList& command_list, sg::SharedPtr<sg::ShaderResourceView> read_texture, SimpleLinearConstantBuffer& cbuffer);

	sg::u32 target_size = 128;
	sg::SharedPtr<sg::Texture> render_texture;
	sg::SharedPtr<sg::RenderTargetView> render_target_view;
	sg::SharedPtr<sg::Device> device;
};