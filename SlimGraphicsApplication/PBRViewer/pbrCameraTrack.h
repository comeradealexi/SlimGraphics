#pragma once
#include "pbrScene.h"
#include "GPUDrivenRendering/gdrJSON.h"
#include <string>

class Camera;

namespace pbr
{
	// The loaded scene's camera track: its preset's track file, or an orbit around the scene once its bounds are known. The
	// "Camera Track" header of the PBR Viewer window edits it (points added at the camera) and saves it to the file.
	class SceneCameraTrack
	{
	public:
		// Follows the scene and its preset (call every frame and after loading)
		void update(const Scene& scene);
		void imgui(const Camera& camera);

		const gdr::CameraTrack& get() const { return track; }
		// Changes whenever the track does (another scene, an edit)
		sg::u32 get_version() const { return version; }

	private:
		void load(const Scene& scene);
		void make_orbit(const Bounds& bounds);
		const char* file() const;

		gdr::CameraTrack track;
		sg::u32 version = 0;
		const ScenePreset* preset = nullptr;	// The scene the track belongs to
		std::string path;
		bool reload = false;
		bool needs_orbit = false;				// Made once the scene is ready (its bounds are known)
		float target_distance = 5.0f;			// Points added in the editor look this far ahead
		std::string message;
	};
}
