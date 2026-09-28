#include "gdrScene.h"
#include <imgui.h>
#include <stack>

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