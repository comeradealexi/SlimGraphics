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


/*

Instanced Mesh Rendering Specifics:

- I want a single dispatch for ALL models - including different materail types
- Meshlet data should remain the same for each loaded model


// Steps:
// Invoke an amplification shader per-instance?

// The final dispatch ID has to basically be the total number of meshlets which will be processed.
// But the number of meshlets can vary per model
// We can also have multiple instances of the same model being rendered
// The meshlet size can vary per-model too.

// Options for the differing meshlet sizes
- Have a maximum number of meshlets allocated per mesh (wasteful)
- Each dispatch ID has a meshlet and index associated with it?


// So final process would be:
	- We dispatch for the total number of meshlets.
	- Each meshlet thread has an instance id and 

	// instance 0 - model 0 - meshlet count 4
	// instance 1 - model 0 - meshlet count 4


// Compute shader called per-instance
	- Each thread has the following data:
		- instance id -> model id (contains meshlet count)

	- compute shader does coarse culling at entire model level

	- compute shader builds up a new intermediate meshlet list
	- each meshlet entry contains:
		- instance id -> model id
		- meshlet index

	- We then have an indirect dispatch mesh call, iterating over everything in our intermediate list
*/

namespace gdr
{
	template <typename T_NODE_FUNCTION>
	static void scene_node_traverse(SceneNode& scene_node, T_NODE_FUNCTION callback)
	{
		for (SceneNode& child_node : scene_node.children)
		{
			scene_node_traverse(child_node, callback);
		}

		callback(scene_node);
	}

	Scene::Scene(sg::SharedPtr<sg::Device>& _device) : device(_device)
	{

	}

	void Scene::update(Ptr<UploadHeap>& upload_heap)
	{
		editor.imgui_scene_overlay(*this);

		SceneGraphTraverser scene_graph_traverser;
		scene_graph_traverser.process_scene_graph(root_scene_node);

		load_gpu_data(scene_graph_traverser, upload_heap);

	}

	void Scene::render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw)
	{

	}

	void Scene::load_gpu_data(SceneGraphTraverser& scene_graph, Ptr<UploadHeap>& upload_heap)
	{

	}

	// Scene Graph Traverser
	void SceneGraphTraverser::process_scene_graph(SceneNode& root_node)
	{
		auto process_node = [this](const SceneNode& scene_node)
			{
				if (scene_node.model_name != kINVALID_MODEL_NAME)
				{
					unique_model_paths.insert(scene_node.model_name);
				}

				using namespace DirectX;

				PerModelData& node_data = nodes.emplace_back();
				node_data.model_matrix = XMMatrixIdentity();
				node_data.scene_node = &scene_node;

				// Construct the final matrix for this node - Walk up the parent nodes
				const SceneNode* current_node = &scene_node;
				while (current_node != nullptr)
				{
					XMMATRIX node_matrix = XMMatrixScaling(scene_node.scale.x, scene_node.scale.y, scene_node.scale.z);

					const XMMATRIX rotation_matrix = XMMatrixRotationRollPitchYaw(XMConvertToRadians(scene_node.rotation.x), XMConvertToRadians(scene_node.rotation.y), XMConvertToRadians(scene_node.rotation.z));
					node_matrix = XMMatrixMultiply(node_matrix, rotation_matrix);

					const XMMATRIX translation_matrix = XMMatrixTranslation(scene_node.position.x, scene_node.position.y, scene_node.position.z);
					node_matrix = XMMatrixMultiply(node_matrix, translation_matrix);

					// Now multiply this nodes matrix in to child
					node_data.model_matrix = XMMatrixMultiply(node_data.model_matrix, node_matrix);

					// Apply parents matrix next
					current_node = current_node->parent;
				}
			};

		scene_node_traverse(root_node, process_node);
	}
}