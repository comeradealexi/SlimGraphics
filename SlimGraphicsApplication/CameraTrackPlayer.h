#pragma once
#include "GPUDrivenRendering/gdrCameraTrack.h"

class Camera;

// Plays a GDR scene's camera track. Time advances by a fixed step per frame rather than by wall clock time, so frame N of
// a playback always shows the same view: captures along a track are reproducible on any machine and backend.
class CameraTrackPlayer
{
public:
	static constexpr float FIXED_TIME_STEP = 1.0f / 60.0f;

	// Stops playback and rewinds
	void set_track(const gdr::CameraTrack& track);
	bool has_track() const { return spline.is_valid(); }
	float get_duration() const { return spline.get_duration(); }

	void play(float start_time = 0.0f);
	void stop() { playing = false; }
	bool is_playing() const { return playing; }

	// Poses the camera at a time on the track (and applies the track's clip planes)
	void pose(Camera& camera, float track_time) const;

	// Once per frame, before Camera::Update: poses the camera and advances the time while playing
	void update(Camera& camera);
	void imgui();

private:
	gdr::CameraSpline spline;
	bool playing = false;
	bool pose_requested = false;
	float time = 0.0f;
	float speed = 1.0f;
};
