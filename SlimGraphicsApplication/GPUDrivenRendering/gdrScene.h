#pragma once
#include <sgPlatformInclude.h>
#include "sgUploadHeap.h"
#include "Model.h"
#include "ShaderSharedStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "DebugDraw.h"
#include "gdrJSON.h"
#include "gdrEditor.h"

namespace gdr
{
	class Scene
	{
		friend class Editor;
	public:
		Scene();
		void update();

	private:
		SceneNode root_scene_node;
		Editor editor;
	};
}