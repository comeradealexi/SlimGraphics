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

private:
	std::vector<std::string> height_textures_list;
	sg::SharedPtr<sg::Device> device;
};