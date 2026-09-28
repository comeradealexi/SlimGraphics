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
/*

GPU Driven Rendering Plan
- Render many instances of the same mesh
- Ideally load a scene file of the scene setup from disk
- Coarse cull each instance on the GPU via compute shader 
- Perhaps also put scene in to a BVH for faster culling
- Occlusion culling with 1 frame delay (like UE does?) - Read back on CPU for the next frame?

Extras:
- Generate a Hi-Z buffer? Then do per-meshlet occlusion culling?

*/

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