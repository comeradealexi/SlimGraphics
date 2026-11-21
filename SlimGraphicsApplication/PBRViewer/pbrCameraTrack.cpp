#include "pbrCameraTrack.h"
#include "Camera.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace pbr
{
	void SceneCameraTrack::update(const Scene& scene)
	{
		if (reload || scene.get_preset() != preset || scene.get_path() != path)
		{
			load(scene);
		}
		if (needs_orbit && scene.is_ready())
		{
			make_orbit(scene.get_bounds());
		}
	}

	void SceneCameraTrack::load(const Scene& scene)
	{
		preset = scene.get_preset();
		path = scene.get_path();
		reload = false;
		track = gdr::CameraTrack();
		message.clear();
		needs_orbit = !(preset && preset->camera_track && gdr::load_camera_track(preset->camera_track, track) && track.is_valid());
		if (needs_orbit && preset && preset->camera_track)
		{
			track = gdr::CameraTrack();
			message = std::string("Could not load ") + preset->camera_track + ": orbiting the scene instead";
		}
		version++;
	}

	// Eight points on a circle outside the bounds, rising and falling, looking at the centre
	void SceneCameraTrack::make_orbit(const Bounds& bounds)
	{
		const XMFLOAT3 centre = bounds.centre();
		const XMFLOAT3 size = bounds.size();
		const float radius = std::max(0.8f * std::sqrt(size.x * size.x + size.z * size.z) + 0.25f * size.y, 0.5f);
		track = gdr::CameraTrack();
		track.duration = 24.0f;
		track.near_plane = std::max(radius * 0.005f, 0.005f);
		track.far_plane = radius * 50.0f;
		for (int i = 0; i < 8; i++)
		{
			const float angle = XM_2PI * i / 8.0f;
			gdr::CameraTrackPoint point;
			point.position = { centre.x + radius * std::sin(angle), centre.y + size.y * (0.15f + 0.25f * std::sin(angle * 2.0f)), centre.z - radius * std::cos(angle) };
			point.target = { centre.x, centre.y + size.y * 0.05f * std::cos(angle * 3.0f), centre.z };
			track.points.push_back(point);
		}
		needs_orbit = false;
		version++;
	}

	const char* SceneCameraTrack::file() const
	{
		return preset && preset->camera_track ? preset->camera_track : "PBRViewer/CameraTracks/Custom.json";
	}

	void SceneCameraTrack::imgui(const Camera& camera)
	{
		if (!ImGui::CollapsingHeader("Camera Track"))
		{
			return;
		}
		ImGui::PushID("PBRCameraTrack");
		ImGui::TextWrapped("%zu points, %.1f s loop. Play it from the Slim Graphics window's Camera Track header. File: %s", track.points.size(), track.duration, file());
		auto drag = [](const char* label, float* value, float speed, float min, float max, const char* format)
			{
				ImGui::SetNextItemWidth(120.0f);
				return ImGui::DragFloat(label, value, speed, min, max, format);
			};
		drag("Target distance", &target_distance, 0.1f, 0.1f, 100.0f, "%.1f m");
		bool edited = false;
		if (ImGui::Button("Add point at camera"))
		{
			// The view matrix's eye and direction (Camera::GetPosition is ahead of the matrix while the camera moves)
			const ShaderStructs::CameraData& data = camera.GetCameraShaderData();
			const auto& eye = data.camera_position;
			const auto& forward = data.camera_direction;
			gdr::CameraTrackPoint point;
			point.position = { eye.x, eye.y, eye.z };
			point.target = { eye.x + forward.x * target_distance, eye.y + forward.y * target_distance, eye.z + forward.z * target_distance };
			if (track.points.empty())
			{
				track.duration = std::max(track.duration, 30.0f);
				track.near_plane = camera.GetNearPlane();
				track.far_plane = std::max(track.far_plane, 1000.0f);
			}
			track.points.push_back(point);
			edited = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove last") && !track.points.empty())
		{
			track.points.pop_back();
			edited = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
		{
			track.points.clear();
			edited = true;
		}
		edited |= drag("Duration", &track.duration, 0.5f, 1.0f, 600.0f, "%.1f s");
		edited |= drag("Near plane", &track.near_plane, 0.001f, 0.001f, 10.0f, "%.3f m");
		edited |= drag("Far plane", &track.far_plane, 1.0f, 1.0f, 100000.0f, "%.0f m");
		if (ImGui::Button("Save"))
		{
			message = (gdr::save_camera_track(track, file()) ? "Saved " : "Could not write ") + std::string(file());
		}
		ImGui::SameLine();
		reload |= ImGui::Button("Reload");
		if (!track.is_valid())
		{
			ImGui::TextDisabled("A track needs at least 3 points");
		}
		if (!message.empty())
		{
			ImGui::TextWrapped("%s", message.c_str());
		}
		version += edited ? 1 : 0;
		ImGui::PopID();
	}
}
