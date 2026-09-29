#pragma once
#include "gdrJSON.h"
#include <stack>

namespace gdr
{
	class Scene;

	struct Action
	{

	};

	class Editor
	{
	public:
		Editor();
		void imgui_scene_overlay(Scene& scene);

	private:
		std::vector<std::string> scene_file_list;
		uint32_t current_scene_file_index = 0;
		std::vector<std::string> model_file_list;
		SceneNode* selected_node = nullptr;
		std::stack<Action> undo_stack;
		std::stack<Action> redo_stack;
	};
}