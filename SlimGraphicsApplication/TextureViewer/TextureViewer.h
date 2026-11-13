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

class TextureViewer
{
public:
	TextureViewer(sg::SharedPtr<sg::Device>& _device);
	void set_texture(sg::SharedPtr<sg::Texture> texture);
	void imgui_update();
	void render(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer);

	sg::D3D12::ImGuiTextureViewer imgui_texture_viewer;
	sg::SharedPtr<sg::Device> device;

	bool resize_target_to_texture = true;
	sg::u32 target_width = 0;
	sg::u32 target_height = 0;

	sg::SharedPtr<sg::Texture> view_texture;
	sg::ShaderResourceView view_texture_srv;

	sg::SharedPtr<sg::Texture> render_texture;
	sg::SharedPtr<sg::RenderTargetView> render_target_view;

	sg::SharedPtr<sg::Pipeline> pipeline;
};