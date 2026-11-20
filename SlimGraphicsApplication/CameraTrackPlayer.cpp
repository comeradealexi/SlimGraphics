#include "CameraTrackPlayer.h"
#include "Camera.h"
#include <imgui.h>
#include <cmath>

void CameraTrackPlayer::set_track(const gdr::CameraTrack& track)
{
	spline.build(track);
	playing = false;
	pose_requested = false;
	time = 0.0f;
}

void CameraTrackPlayer::play(float start_time)
{
	if (!has_track())
	{
		return;
	}
	time = start_time;
	playing = true;
}

void CameraTrackPlayer::pose(Camera& camera, float track_time) const
{
	if (!has_track())
	{
		return;
	}
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 target;
	spline.evaluate(track_time, position, target);
	camera.SetLookAt(position, target);
	camera.SetDepthPlanes(spline.get_track().near_plane, spline.get_track().far_plane);
}

void CameraTrackPlayer::update(Camera& camera)
{
	if (!playing && !pose_requested)
	{
		return;
	}
	pose(camera, time);
	pose_requested = false;
	if (playing)
	{
		time = std::fmod(time + FIXED_TIME_STEP * speed, spline.get_duration());
	}
}

void CameraTrackPlayer::imgui()
{
	if (!ImGui::CollapsingHeader("Camera Track"))
	{
		return;
	}
	ImGui::PushID("CameraTrack");
	if (!has_track())
	{
		ImGui::TextUnformatted("The loaded GDR scene has no camera track");
	}
	else
	{
		ImGui::Text("%zu points, %.1f s loop, clip planes %.2f - %.0f", spline.get_track().points.size(), spline.get_duration(),
			spline.get_track().near_plane, spline.get_track().far_plane);
		if (ImGui::Button(playing ? "Pause" : "Play"))
		{
			playing = !playing;
		}
		ImGui::SameLine();
		if (ImGui::Button("Restart"))
		{
			time = 0.0f;
			pose_requested = true;
		}
		if (ImGui::SliderFloat("Time", &time, 0.0f, spline.get_duration(), "%.2f s"))
		{
			pose_requested = true;
		}
		ImGui::SliderFloat("Speed", &speed, 0.1f, 4.0f, "%.1fx");
		ImGui::TextDisabled("Time advances %.4f s per frame (deterministic)", FIXED_TIME_STEP);
	}
	ImGui::PopID();
}
