#include "gdrEditor.h"
#include "gdrScene.h"
#include <imgui.h>
#include <stack>
#include <format>
#include "seEngineBasicFileIO.h"

namespace gdr
{
	inline const char* StringPathToFilename(std::string& str)
	{
		// Skip the leading "../SlimGraphicsAssets/" style prefix (std::filesystem paths use either separator).
		size_t slashpos = str.find_first_of("/\\");
		if (slashpos != std::string::npos)
		{
			slashpos = str.find_first_of("/\\", slashpos + 1);
		}
		if (slashpos == std::string::npos) slashpos = 0;
		else slashpos++;
		return str.c_str() + slashpos;
	}

	Editor::Editor()
	{
		
scene_file_list = se::BasicFileIO::find_files_recursive("..\\SlimGraphicsAssets\\GDRAssets", { ".json" });
		const std::vector<const char*> model_extensions = { ".obj", ".dae", ".fbx", ".gltf" };
		model_file_list = se::BasicFileIO::find_files_recursive("../SlimGraphicsAssets", model_extensions);
	}

	void Editor::imgui_scene_overlay(Scene& scene)
	{
		if (!ImGui::Begin("GDR Scene Editor"))
		{
			ImGui::End(); // End() must always be paired with Begin(), even when it returns false
			return;
		}

		if (ImGui::CollapsingHeader("Save/Load", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static char file_path_buffer[256] = {};

			ImGui::BeginDisabled(scene_file_list.size() == 0);
			if (ImGui::BeginCombo("Select Existing File", scene_file_list.size() ? StringPathToFilename(scene_file_list[current_scene_file_index]) : "", ImGuiComboFlags_HeightLargest))
			{
				for (int n = 0; n < scene_file_list.size(); n++)
				{
					const bool is_selected = (current_scene_file_index == n);
					if (ImGui::Selectable(StringPathToFilename(scene_file_list[n]), is_selected))
					{
						current_scene_file_index = n;
						strncpy_s(file_path_buffer, scene_file_list[n].c_str(), _TRUNCATE);
					}

					// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
					if (is_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();

			ImGui::InputText("File Name", file_path_buffer, 256);
			ImGui::BeginDisabled(file_path_buffer[0] == '\0');
			static const char* last_message = nullptr;
			if (ImGui::Button("Save"))
			{
				if (serialise(scene.root_scene_node, scene.properties, file_path_buffer))
				{
					last_message = "Save Successful";
				}
				else
				{
					last_message = "Save Failed";
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Load"))
			{
				selected_node = nullptr;
				if (scene.load_scene(file_path_buffer))
				{
					last_message = "Load Successful";
				}
				else
				{
					last_message = "Load Failed";
				}
			}
			ImGui::EndDisabled();
			if (last_message)
			{
				ImGui::Text(last_message);
			}
		}

		const ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_DrawLinesToNodes | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

		std::stack<std::pair<SceneNode*, bool>> node_stack;
		node_stack.push({&scene.root_scene_node, false });
		
		if (selected_node == nullptr)
			selected_node = &scene.root_scene_node;

		SceneNode* new_node_added = nullptr;
		bool delete_selected_node = false;
		bool duplicate_selected_node = false;
		if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int32_t current_model_index = -1;
			for (int32_t i = 0; i < model_file_list.size(); i++)
			{
				if (selected_node->model_name == model_file_list[i])
				{
					current_model_index = i;
					break;
				}
			}

			if (ImGui::BeginCombo("Model File Path", current_model_index >= 0 ? StringPathToFilename(model_file_list[current_model_index]) : selected_node->model_name.c_str(), ImGuiComboFlags_HeightLargest))
			{
				for (int n = 0; n < model_file_list.size(); n++)
				{
					const bool is_selected = (current_model_index == n);
					if (ImGui::Selectable(StringPathToFilename(model_file_list[n]), is_selected))
					{
						current_model_index = n;
						selected_node->model_name = model_file_list[n];
					}

					// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
					if (is_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::TextDisabled("Model: %s", selected_node->model_name.c_str());
			ImGui::DragFloat3("Position", &selected_node->position.x, 0.01f);
			ImGui::DragFloat3("Rotation", &selected_node->rotation.x, 0.1f);
			ImGui::DragFloat3("Scale", &selected_node->scale.x, 0.01f);
			ImGui::ColorEdit3("Colour", &selected_node->colour.x);

			// Components
			if (selected_node->light)
			{
				LightDesc& light = *selected_node->light;
				ImGui::SeparatorText("Light");
				bool directional = light.type == "directional";
				if (ImGui::Checkbox("Directional", &directional))
				{
					light.type = directional ? "directional" : "point";
				}
				ImGui::ColorEdit3("Light Colour", &light.colour.x);
				ImGui::DragFloat("Intensity", &light.intensity, 0.01f, 0.0f, 100.0f);
				if (directional)
				{
					ImGui::DragFloat3("Direction", &light.direction.x, 0.01f, -1.0f, 1.0f);
				}
				else
				{
					ImGui::DragFloat("Range", &light.range, 0.05f, 0.01f, 1000.0f);
				}
				if (ImGui::Button("Remove Light"))
				{
					selected_node->light.reset();
				}
			}
			else if (ImGui::Button("Add Light"))
			{
				selected_node->light = LightDesc();
			}
			if (selected_node->terrain)
			{
				const TerrainDesc& terrain = *selected_node->terrain;
				ImGui::SeparatorText("Terrain");
				ImGui::TextDisabled("%.0f x %.0f units, %u quads, %.1f high, seed %u", terrain.size, terrain.size, terrain.resolution, terrain.height, terrain.seed);
			}
			ImGui::Separator();

			ImGui::TextDisabled("Address: %p", selected_node);
			ImGui::TextDisabled("Parent Address: %p", selected_node->parent);
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
				new_node_added->parent = selected_node;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(selected_node == &scene.root_scene_node); // Can't duplicate root
			if (ImGui::Button("Duplicate"))
			{
				duplicate_selected_node = true;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
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

					if (ImGui::TreeNodeEx((const void*)cur_node, tree_flags | extra_flags, "%s", cur_node->model_name.c_str()))
					{
						auto it = cur_node->children.begin();

						while (it != cur_node->children.end())
						{
							if ((duplicate_selected_node || delete_selected_node) && &(*it) == selected_node)
							{
								if (delete_selected_node)
								{
									delete_selected_node = false;
									it = cur_node->children.erase(it);
									selected_node = nullptr;
									if (it != cur_node->children.end()) //Update selected note to be next child after a delete
									{
										selected_node = &(*it);
									}
								}
								else if (duplicate_selected_node)
								{
									duplicate_selected_node = false;
									// The copy's descendants still point at the original subtree, so rebuild the parent links.
									SceneNode& duplicate = cur_node->children.emplace_back(*it);
									set_parent_pointers(duplicate, cur_node);
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