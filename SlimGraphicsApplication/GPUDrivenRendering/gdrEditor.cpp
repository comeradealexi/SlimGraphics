#include "gdrEditor.h"
#include "gdrScene.h"
#include <imgui.h>
#include <stack>
#include <format>

namespace gdr
{
	void Editor::imgui_scene_overlay(Scene& scene)
	{
		if (!ImGui::Begin("Scene Overlay"))
		{
			return;
		}
		const ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_DrawLinesToNodes | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

		std::stack<std::pair<SceneNode*, bool>> node_stack;
		node_stack.push({&scene.root_scene_node, false });
		
		if (selected_node == nullptr)
			selected_node = &scene.root_scene_node;

		SceneNode* new_node_added = nullptr;
		bool delete_selected_node = false;
		if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Model: %s", selected_node->model_name.c_str());
			ImGui::DragFloat3("Position", &selected_node->position.x);
			ImGui::DragFloat3("Rotation", &selected_node->rotation.x);
			ImGui::DragFloat3("Scale", &selected_node->scale.x);
			ImGui::ColorEdit3("Colour", &selected_node->colour.x);
			ImGui::BeginDisabled(selected_node == &scene.root_scene_node); // Can't delete root
			if (ImGui::Button("Delete"))
			{
				delete_selected_node = true;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Add Child"))
			{
				new_node_added = &(selected_node->children.emplace_back());
			}
			ImGui::SameLine();
			ImGui::TextDisabled("ID: %llu", selected_node->unique_id);
		}

		if (ImGui::CollapsingHeader("Scene Graph", ImGuiTreeNodeFlags_DefaultOpen))
		{
			while (node_stack.empty() == false)
			{
				const int starting_stack_size = node_stack.size();
				SceneNode* cur_node = node_stack.top().first;
				if (node_stack.top().second == false)
				{
					node_stack.top().second = true;
					ImGuiTreeNodeFlags extra_flags = cur_node == selected_node ? ImGuiTreeNodeFlags_Selected : 0;
					extra_flags |= (cur_node->children.size() == 0) ? ImGuiTreeNodeFlags_Leaf : 0;

					if (new_node_added) // Mark us as open if we're the parent of new added node
					{
						if (std::find(cur_node->children.cbegin(), cur_node->children.cend(), *new_node_added) != cur_node->children.cend())
						{
							ImGui::SetNextItemOpen(true);
						}
					}

					if (ImGui::TreeNodeEx((const void*)cur_node, tree_flags | extra_flags, "%llu: %s", cur_node->unique_id, cur_node->model_name.c_str()))
					{
						auto it = cur_node->children.begin();

						while (it != cur_node->children.end())
						{
							if (delete_selected_node && &(*it) == selected_node)
							{
								delete_selected_node = false;
								it = cur_node->children.erase(it);
								selected_node = nullptr;
								if (it != cur_node->children.end()) //Update selected note to be next child after a delete
								{
									selected_node = &(*it);
								}
							}
							else
							{
								node_stack.push({ &(*it), false });
								it++;
							}
						}
					}
					else
					{
						node_stack.pop();
					}
					if (ImGui::IsItemClicked())
					{
						selected_node = cur_node;
					}
				}
				else
				{
					ImGui::TreePop();
					node_stack.pop();
				}
			}
		}

		ImGui::End();
	}
}