#include "TestRunner.h"
#include "Camera.h"
#include "CameraTrackPlayer.h"
#include "ModelViewer.h"
#include "PostProcess/PostProcess.h"
#include "MagnifyingGlass/MagnifyingGlass.h"
#include "GPUDrivenRendering/gdrScene.h"
#include "DebugDraw.h"
#include <seEngineBasicFileIO.h>
#include <algorithm>
#include <string_view>
#include <imgui.h>

namespace
{
	// Merges 'overrides' into 'base' recursively (objects merge, anything else replaces)
	void merge(nlohmann::json& base, const nlohmann::json& overrides)
	{
		for (auto it = overrides.begin(); it != overrides.end(); ++it)
		{
			if (it.value().is_object() && base.contains(it.key()) && base[it.key()].is_object())
			{
				merge(base[it.key()], it.value());
			}
			else
			{
				base[it.key()] = it.value();
			}
		}
	}

	// Logs the keys of 'object' that are not in 'known': a misspelt key, or a setting nothing applies, would otherwise
	// silently test the default instead
	bool has_only_known_keys(const nlohmann::json& object, std::initializer_list<std::string_view> known, const std::string& where)
	{
		bool ok = true;
		for (auto it = object.begin(); it != object.end(); ++it)
		{
			if (std::find(known.begin(), known.end(), it.key()) == known.end())
			{
				seWriteLine("[TEST] ERROR plan: %s: unknown key \"%s\"", where.c_str(), it.key().c_str());
				ok = false;
			}
		}
		return ok;
	}

	// Every key a case may use. The settings lists must match ModelViewer::ApplyTestSettings and
	// gdr::Scene::apply_test_settings.
	bool validate_case(const nlohmann::json& c)
	{
		const std::string name = c["name"].get<std::string>();
		bool ok = has_only_known_keys(c, { "name", "frames", "model_viewer", "gdr", "gdr_scene", "camera", "depth_clear", "debug_draw",
			"model_viewer_settings", "gdr_settings", "post_process", "magnifier", "capture_magnifier", "nondeterministic", "timing_dependent_stats" }, name);
		const auto section = [&](const char* key, std::initializer_list<std::string_view> known)
		{
			if (c.contains(key) && c[key].is_object())
			{
				ok = has_only_known_keys(c[key], known, name + "." + key) && ok;
			}
		};
		section("camera", { "track_time", "position", "target", "near", "far" });
		section("post_process", { "enabled", "technique", "dispatch_mode" });
		section("magnifier", { "active", "x", "y", "zoom" });
		section("gdr_settings", { "debug_mode", "cull_all", "frustum_culling", "cone_culling", "sort_mode", "render_mode", "wireframe",
			"depth_enable", "depth_write" });
		section("model_viewer_settings", { "model", "mesh_mode", "cone_culling", "sphere_culling", "render_mode", "wave_mode", "render_geo",
			"pixel_pipeline", "simplified_value", "early_depth_stencil", "geometry_shader", "geometry_shader_scale", "geometry_shader_explode",
			"wireframe", "cull_mode", "depth_comparison", "depth_enable", "depth_write", "instance_count", "render_percentage", "rotation",
			"textures", "debug_aabb", "debug_obb", "debug_sphere", "debug_meshlets", "cpu_sort" });
		if (c.contains("camera") && !c["camera"].is_object() && c["camera"] != "default")
		{
			seWriteLine("[TEST] ERROR plan: %s: \"camera\" must be \"default\" or an object", name.c_str());
			ok = false;
		}
		return ok;
	}

	DirectX::XMFLOAT3 read_float3(const nlohmann::json& j, DirectX::XMFLOAT3 fallback)
	{
		if (!j.is_array() || j.size() != 3 || !j[0].is_number() || !j[1].is_number() || !j[2].is_number())
		{
			return fallback;
		}
		return DirectX::XMFLOAT3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
	}
}

bool TestRunner::load(const std::string& plan_path, const std::string& output)
{
	const std::vector<uint8_t> data = se::BasicFileIO::load_file(plan_path.c_str());
	// Parse without exceptions (the engine builds without them)
	const nlohmann::json plan = nlohmann::json::parse(data, nullptr, false);
	if (data.empty() || plan.is_discarded() || !plan.is_object() || !plan.contains("cases") || !plan["cases"].is_array())
	{
		seWriteLine("[TEST] ERROR plan: cannot read test plan %s", plan_path.c_str());
		return false;
	}
	const nlohmann::json defaults = plan.contains("defaults") && plan["defaults"].is_object() ? plan["defaults"] : nlohmann::json::object();
	bool valid = true;
	for (const nlohmann::json& test_case : plan["cases"])
	{
		if (!test_case.is_object() || !test_case.contains("name") || !test_case["name"].is_string())
		{
			seWriteLine("[TEST] ERROR plan: every case needs a \"name\"");
			return false;
		}
		nlohmann::json merged = defaults;
		merge(merged, test_case);
		valid = validate_case(merged) && valid; // Keep going to report every problem at once
		cases.push_back(std::move(merged));
	}
	if (!valid)
	{
		return false;
	}
	output_dir = output;
	active = true;
	// Hover highlights depend on where the mouse is: ignore it so captures of the UI are repeatable
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
	seWriteLine("[TEST] PLAN %s: %zu cases", plan_path.c_str(), cases.size());
	return true;
}

void TestRunner::error(const std::string& message)
{
	seWriteLine("[TEST] ERROR %s: %s", cases[case_index].value("name", "?").c_str(), message.c_str());
}

void TestRunner::begin_frame(Systems& systems)
{
	if (!active || is_finished())
	{
		return;
	}
	if (case_frame < 0)
	{
		start_case(systems);
	}
	apply_camera(systems);
	pending_capture = case_frame == case_frames - 1 ? cases[case_index]["name"].get<std::string>() : std::string();
}

void TestRunner::start_case(Systems& systems)
{
	const nlohmann::json& c = cases[case_index];
	seWriteLine("[TEST] BEGIN %s", c["name"].get<std::string>().c_str());
	case_frame = 0;
	case_frames = std::max(2, c.value("frames", 6));

	systems.enable_model_viewer = c.value("model_viewer", true);
	systems.enable_gdr = c.value("gdr", false);
	systems.depth_clear_value = c.value("depth_clear", 1.0f);
	systems.debug_draw.SetEnabled(c.value("debug_draw", false));

	// GDR scene (an empty name clears the scene). Models load during the scene's next update.
	const std::string scene = c.value("gdr_scene", "");
	if (scene != current_gdr_scene)
	{
		if (scene.empty())
		{
			systems.gdr_scene.clear_scene();
		}
		else if (!systems.gdr_scene.load_scene(scene.c_str()))
		{
			error("cannot load GDR scene " + scene);
		}
		current_gdr_scene = scene;
		systems.camera_track_player.set_track(systems.gdr_scene.get_camera_track());
	}
	if (c.contains("gdr_settings")) systems.gdr_scene.apply_test_settings(c["gdr_settings"]);
	if (c.contains("model_viewer_settings") && !systems.model_viewer.ApplyTestSettings(c["model_viewer_settings"]))
	{
		error("model viewer settings failed (unknown model?)");
	}

	if (c.contains("post_process"))
	{
		const nlohmann::json& p = c["post_process"];
		systems.post_process.enabled = p.value("enabled", true);
		systems.post_process.post_process_technique = static_cast<PostProcess::PostProcessTechnique>(p.value("technique", 0));
		const auto dispatch = static_cast<PostProcess::DispatchMode>(std::clamp(p.value("dispatch_mode", 2), 0, static_cast<int>(PostProcess::DispatchMode::Count) - 1));
		if (dispatch != systems.post_process.dispatch_mode)
		{
			systems.post_process.dispatch_mode = dispatch;
			systems.post_process.LoadPipeline();
		}
	}
	if (c.contains("camera") && c["camera"].is_object() && c["camera"].contains("track_time") && !systems.camera_track_player.has_track())
	{
		error("the case poses the camera on a track but the GDR scene has none");
	}
	systems.camera_track_player.stop();
}

void TestRunner::apply_camera(Systems& systems) const
{
	const nlohmann::json& c = cases[case_index];
	const nlohmann::json camera = c.contains("camera") ? c["camera"] : nlohmann::json("default");
	if (camera.is_object() && camera.contains("track_time") && camera["track_time"].is_number())
	{
		systems.camera_track_player.pose(systems.camera, camera["track_time"].get<float>());
		return;
	}
	// A fixed pose; "default" is the application's start up camera
	DirectX::XMFLOAT3 position(0.0f, 0.25f, -2.0f);
	DirectX::XMFLOAT3 target(0.0f, 0.25f, 0.0f);
	float near_plane = 0.1f;
	float far_plane = 100.0f;
	if (camera.is_object())
	{
		position = read_float3(camera.value("position", nlohmann::json()), position);
		target = read_float3(camera.value("target", nlohmann::json()), target);
		near_plane = camera.value("near", near_plane);
		far_plane = camera.value("far", far_plane);
	}
	systems.camera.SetLookAt(position, target);
	systems.camera.SetDepthPlanes(near_plane, far_plane);
}

void TestRunner::after_update(Systems& systems)
{
	if (!active || is_finished())
	{
		return;
	}
	// The magnifier takes its state from its ImGui header and the mouse every update: override it afterwards
	const nlohmann::json& c = cases[case_index];
	const nlohmann::json magnifier = c.contains("magnifier") ? c["magnifier"] : nlohmann::json::object();
	systems.magnifying_glass.active = magnifier.value("active", false);
	if (systems.magnifying_glass.active)
	{
		systems.magnifying_glass.constant_data.coordinates.x = magnifier.value("x", 960.0f);
		systems.magnifying_glass.constant_data.coordinates.y = magnifier.value("y", 540.0f);
		systems.magnifying_glass.constant_data.coordinates.z = static_cast<float>(systems.magnifying_glass.target_size);
		systems.magnifying_glass.constant_data.coordinates.w = static_cast<float>(systems.magnifying_glass.target_size);
		systems.magnifying_glass.constant_data.scale.x = magnifier.value("zoom", 0.5f);
	}
}

void TestRunner::end_frame(Systems& systems, const sg::PipelineStatistics& s)
{
	if (!active || is_finished())
	{
		return;
	}
	if (!pending_capture.empty())
	{
		// Statistics of the captured frame: GDR scene totals and GPU culling feedback (debug mode), pipeline statistics and
		// the model viewer's GPU counters
		const std::string& name = pending_capture;
		const auto& gdr = systems.gdr_scene.get_statistics();
		const auto feedback = systems.gdr_scene.read_debug_feedback();
		const ModelViewer::UAVReadBack mv = systems.model_viewer.ReadUAVCounters();
		seWriteLine("[TEST] STAT %s gdr_instances=%u gdr_mesh_parts=%u gdr_meshlets=%u gdr_culled_total=%u gdr_culled_frustum=%u gdr_culled_cone=%u gdr_lights=%u gdr_texture_slices=%u gdr_terrain_tiles=%u",
			name.c_str(), gdr.total_instances, gdr.total_mesh_parts, gdr.total_meshlets, feedback.meshlets_culled_total, feedback.meshlets_cullled_by_frustum, feedback.meshlets_culled_by_cone,
			gdr.total_lights, gdr.texture_slices, gdr.terrain_tiles);
		seWriteLine("[TEST] STAT %s ia_vertices=%llu ia_primitives=%llu vs_invocations=%llu gs_invocations=%llu gs_primitives=%llu ps_invocations=%llu cs_invocations=%llu as_invocations=%llu ms_invocations=%llu",
			name.c_str(), s.ia_vertices, s.ia_primitives, s.vs_invocations, s.gs_invocations, s.gs_primitives, s.ps_invocations, s.cs_invocations, s.as_invocations, s.ms_invocations);
		seWriteLine("[TEST] STAT %s mv_pixels=%u mv_vs=%u mv_ms=%u mv_as=%u mv_ms_prims=%u mv_cull_cone=%u mv_cull_sphere=%u mv_gs_in=%u mv_gs_out=%u",
			name.c_str(), mv.UAV_INDEX_PIXELS_SHADED, mv.UAV_INDEX_VERTEX_SHADER_INVOCATIONS, mv.UAV_INDEX_MESH_SHADER_INVOCATIONS, mv.UAV_INDEX_AMPLIFICATION_SHADER_INVOCATIONS,
			mv.UAV_INDEX_MESH_SHADER_PRIM_COUNT, mv.UAV_INDEX_MESH_SHADER_CULL_CONE_COUNT, mv.UAV_INDEX_MESH_SHADER_CULL_SPHERE_COUNT, mv.UAV_INDEX_GEOMETRY_SHADER_TRIANGLES_IN, mv.UAV_INDEX_GEOMETRY_SHADER_TRIANGLES_OUT);
		seWriteLine("[TEST] END %s", name.c_str());
		pending_capture.clear();
		case_index++;
		case_frame = -1;
		if (is_finished())
		{
			seWriteLine("[TEST] COMPLETE %zu cases", cases.size());
		}
		return;
	}
	case_frame++;
}
