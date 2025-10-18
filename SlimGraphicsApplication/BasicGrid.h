#pragma once
#include <sgPlatformInclude.h>
#include "sgUploadHeap.h"
#include "Model.h"
#include "ShaderSharedStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"

class BasicGrid
{
public:
	BasicGrid(sg::SharedPtr<sg::Device>& _device);
	void Update(float delta_time, float total_time, const Camera& camera);
	void Render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer);

private:
	sg::SharedPtr<sg::Pipeline> pipeline;
};