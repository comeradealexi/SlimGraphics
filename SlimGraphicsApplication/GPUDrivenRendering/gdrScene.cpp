#include "gdrScene.h"
#include <imgui.h>
#include <stack>

/*

GPU Driven Rendering Plan
- Render many instances of the same mesh
- Ideally load a scene file of the scene setup from disk
- Coarse cull each instance on the GPU via compute shader
- Perhaps also put scene in to a BVH for faster culling
- Occlusion culling with 1 frame delay (like UE does?) - Read back on CPU for the next frame?

Extras:
- Generate a Hi-Z buffer? Then do per-meshlet occlusion culling?



Specific Rendering Process

 - Loop over the screen file and put the data in to a raw GPU buffer containing all required information
 - - Apply parent transforms and properties
 - - Ensure sphere & aabb or obb is present

 GPU Stage 1:
 - Per-object frustum & distance culling
 
 GPU Stage 2:
 - Render Scene (use occlusion data from previous frame)

 GPU Stage 2 
 - obb occlusion testing




 Buffers Needed
 - GPU Data Buffer With All Draw Data + unique id
 - GPU Occlusion Writeback Buffer

*/

namespace gdr
{
	Scene::Scene()
	{
		//deserialise(root_scene_node, "");
	}

	void Scene::update()
	{
		editor.imgui_scene_overlay(*this);
	}
}