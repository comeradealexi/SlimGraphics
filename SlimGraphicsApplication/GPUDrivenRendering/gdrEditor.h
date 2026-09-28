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
		void imgui_scene_overlay(Scene& scene);

	private:
		SceneNode* selected_node = nullptr;
		std::stack<Action> undo_stack;
		std::stack<Action> redo_stack;
	};
}