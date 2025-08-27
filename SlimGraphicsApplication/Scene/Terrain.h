#pragma once
#include <sgPlatformInclude.h>
#include "UploadHeap.h"
#include "Model.h"
#include "ShaderSharedStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "DebugDraw.h"

class Terrain
{
public:
	Terrain(sg::SharedPtr<sg::Device>& _device);

	void Update(float delta_time, float total_time, const Camera& camera, DebugDraw& debug_draw);
	void Render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw);

	void LoadHeightmapTexture(sg::Ptr<UploadHeap>& upload_heap);

private:
	sg::u32 current_texture_index = 0;
	std::vector<std::string> height_textures_list;
	sg::SharedPtr<sg::Device> device;

	sg::SharedPtr<sg::Texture> terrain_texture;
	sg::SharedPtr<sg::TextureView> terrain_texture_view;
	
	sg::Ptr<sg::Pipeline> terrain_pipeline;
	sg::Ptr<sg::VertexShader> shader_vertex;
	sg::Ptr<sg::PixelShader> shader_pixel;
};