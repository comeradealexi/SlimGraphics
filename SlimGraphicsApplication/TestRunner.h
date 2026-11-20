#pragma once
#include <sgPlatformInclude.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class Camera;
class CameraTrackPlayer;
class ModelViewer;
class PostProcess;
class MagnifyingGlass;
class DebugDraw;
namespace gdr { class Scene; }

// Runs a test plan inside one application session (-test_plan=PATH -test_output=DIR). A plan is a JSON object with
// "defaults" and a list of "cases"; each case is merged over the defaults (so every case fully describes its state and
// cases do not depend on their order), configures the renderer, renders "frames" frames (letting loads and the HZB settle)
// and on its last frame captures <name>_final.png (scene render target), <name>_output.png (after post processing),
// <name>_backbuffer.png (with ImGui) and, with "capture_magnifier", <name>_magnifier.png. Progress, statistics and errors
// are logged as lines starting with "[TEST]" (see Tools/validate.ps1, which runs Tools/validation_tests.json on every
// backend and compares the captures and statistics between them).
class TestRunner
{
public:
	// Everything a test case can configure
	struct Systems
	{
		Camera& camera;
		CameraTrackPlayer& camera_track_player;
		ModelViewer& model_viewer;
		gdr::Scene& gdr_scene;
		PostProcess& post_process;
		MagnifyingGlass& magnifying_glass;
		DebugDraw& debug_draw;
		bool& enable_model_viewer;
		bool& enable_gdr;
		float& depth_clear_value;
	};

	bool load(const std::string& plan_path, const std::string& output_dir);
	bool is_active() const { return active; }
	bool is_finished() const { return active && case_index >= cases.size(); }

	// Start of the frame, before the systems update: starts the next case when due (applying its settings)
	void begin_frame(Systems& systems);
	// After the systems updated (they apply ImGui and input state): re-applies the case's per frame state (camera, magnifier)
	void after_update(Systems& systems);
	// Non empty on the frame to capture: the capture file name prefix (the case name)
	const std::string& capture_name() const { return pending_capture; }
	std::string capture_path(const char* suffix) const { return output_dir + "/" + pending_capture + suffix; }
	// Whether the case also captures the magnifier's render target ("capture_magnifier")
	bool capture_magnifier() const { return active && !is_finished() && cases[case_index].value("capture_magnifier", false); }
	// End of the frame, after the GPU finished it (and its capture was written): logs statistics and advances
	void end_frame(Systems& systems, const sg::PipelineStatistics& pipeline_statistics);

private:
	void start_case(Systems& systems);
	void apply_camera(Systems& systems) const;
	void error(const std::string& message);

	bool active = false;
	std::string output_dir;
	std::vector<nlohmann::json> cases; // Merged with the plan defaults
	size_t case_index = 0;
	int case_frame = -1; // Frame within the current case (-1 = not started)
	int case_frames = 0;
	std::string current_gdr_scene;
	std::string pending_capture;
};
