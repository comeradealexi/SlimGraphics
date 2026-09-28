#include "gdrScene.h"
#include <imgui.h>
#include <stack>

namespace gdr
{
	void Scene::imgui_scene_overlay()
	{
		if (!ImGui::Begin("Scene Overlay"))
		{
			return;
		}
		const ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_DrawLinesToNodes | ImGuiTreeNodeFlags_DrawLinesFull;

		std::stack<SceneNode*> node_stack;
		node_stack.push(&root_scene_node);

		auto imgui_node_properties = [](SceneNode* node)
			{
				ImGui::DragFloat3("Position", &node->position.x);
				ImGui::DragFloat3("Rotation", &node->rotation.x);
				ImGui::DragFloat3("Scale", &node->scale.x);
				ImGui::ColorEdit3("Colour", &node->colour.x);
			};

		if (ImGui::TreeNode(root_scene_node.model_name.c_str()))
		{



		}

		ImGui::End();
	}
}