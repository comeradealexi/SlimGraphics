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
	Scene::Scene(sg::SharedPtr<sg::Device>& _device) : device(_device)
	{
		//deserialise(root_scene_node, "");
	}

	void Scene::update(Ptr<UploadHeap>& upload_heap)
	{
		editor.imgui_scene_overlay(*this);

		load_models(upload_heap);
	}

	void Scene::render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw)
	{

	}

	Ptr<Model> Scene::load_model(Ptr<UploadHeap>& upload_heap, const char* file_path)
	{
		Model::InitData model_init_data = {};
		model_init_data.file_path = file_path;
		return Ptr<Model>(new Model(device.get(), upload_heap.get(), model_init_data, nullptr));
	}

	void Scene::load_models(Ptr<UploadHeap>& upload_heap)
	{
		for (auto& [name, model] : models)
		{
			if (model == nullptr)
			{
				model = load_model(upload_heap, name.c_str());
			}
		}
	}

}