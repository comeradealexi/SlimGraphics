#pragma once
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
	void Render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw);

private:

};