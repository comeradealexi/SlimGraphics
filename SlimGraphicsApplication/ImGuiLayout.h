#pragma once
#include <imgui.h>

// The main window's area below the menu bar (computed directly: ImGui only takes the menu bar out of the viewport's work
// area from the frame after it first appears, which is when first use placement happens)
inline ImVec4 layout_area()
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float menu_bar = ImGui::GetFrameHeight();
	return ImVec4(viewport->Pos.x, viewport->Pos.y + menu_bar, viewport->Size.x, viewport->Size.y - menu_bar);
}

// Where a window opens the first time (afterwards imgui.ini keeps wherever the user put it). x is measured from the left
// edge of the area below the menu bar, or from its right edge when negative; a height of 0 reaches the bottom.
inline void place_window_first_use(float x, float y, float width, float height = 0.0f)
{
	const ImVec4 area = layout_area();
	const float left = x >= 0.0f ? area.x + x : area.x + area.z + x - width;
	ImGui::SetNextWindowPos(ImVec2(left, area.y + y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(width, height > 0.0f ? height : area.w - y - 10.0f), ImGuiCond_FirstUseEver);
}

// Centred in the area below the menu bar
inline void centre_window_first_use(float width, float height)
{
	const ImVec4 area = layout_area();
	ImGui::SetNextWindowPos(ImVec2(area.x + area.z * 0.5f, area.y + area.w * 0.4f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
}
