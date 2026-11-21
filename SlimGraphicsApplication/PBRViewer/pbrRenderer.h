#pragma once
#include <sgPlatformInclude.h>
#include "sgUploadHeap.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "pbrScene.h"
#include "pbrShaderStructures.h"
#include "GPUDrivenRendering/gdrJSON.h"
#include <nlohmann/json.hpp>
#include <array>
#include <string>
#include <vector>

namespace pbr
{
	// Physically based viewer (D3D12 only for now): GPU driven mesh shading into a visibility buffer with two phase occlusion
	// culling, then ray traced sun shadows, diffuse global illumination (with a radiance cache for further bounces) and
	// reflections at one sample per pixel, denoised spatiotemporally, composited, anti-aliased (TAA), exposed and tone mapped.
	// A progressive reference path tracer shares the materials, BRDF, sun and sky. Every setting is a cvar ("pbr." prefix).
	class Renderer
	{
	public:
		Renderer(sg::SharedPtr<sg::Device> device, sg::SharedPtr<sg::CommandQueue> queue, sg::u32 width, sg::u32 height);
		~Renderer();

		// Hardware ray tracing and bindless resources are required (D3D12 only for now)
		bool is_supported() const { return supported; }

		// A preset name (scene_presets) or a file path. Presets also place the camera and the sun.
		bool load_scene(const std::string& name_or_path, Camera* camera);
		bool is_ready() const { return scene && scene->is_ready(); }
		// Places the camera at the loaded preset's view (or the model file's camera)
		void apply_preset_camera(Camera& camera) const;
		bool preset_camera(DirectX::XMFLOAT3& position, DirectX::XMFLOAT3& target) const;
		const Scene* get_scene() const { return scene.get(); }
		// The scene's camera track: the preset's track file, or an orbit around the scene once its bounds are known. The
		// version changes whenever the track does (another scene, edits in the "Camera Track" header).
		const gdr::CameraTrack& get_camera_track() const { return camera_track; }
		sg::u32 get_camera_track_version() const { return camera_track_version; }

		// ImGui windows, cvars, results of the previous frame (call once per frame before render)
		void update(float delta_time, Camera& camera);
		// Renders the frame into 'output' (an RGBA8 UNORM UAV the size of the viewer)
		void render(sg::CommandList& command_list, const Camera& camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, sg::UnorderedAccessView& output);

		// Automated tests: {"scene": preset or path, "settings": {cvar: value}} (cvars not given are reset)
		bool apply_test_settings(const nlohmann::json& settings, Camera& camera, std::string& error);
		// Frames the path tracer has accumulated
		sg::u32 get_accumulated_frames() const { return accumulated_frames; }
		std::string get_statistics_line() const;
		// GPU time of the last finished frame's passes
		double gpu_milliseconds() const;

	private:
		struct Target
		{
			sg::SharedPtr<sg::Texture> texture;
			sg::ShaderResourceView srv;
			sg::UnorderedAccessView uav;
			sg::SharedPtr<sg::RenderTargetView> rtv;
			sg::SharedPtr<sg::DepthStencilView> dsv;
			sg::u32 bindless = ~0u;
		};
		struct BufferResource
		{
			sg::SharedPtr<sg::Buffer> buffer;
			sg::UnorderedAccessView uav;
			sg::ShaderResourceView srv;
		};
		// One denoised signal's temporal and filter targets
		struct DenoiseTargets
		{
			Target accumulated;
			Target moments[2];
			Target history[2];
			Target ping;
			Target pong;
		};
		struct Binding
		{
			std::array<sg::ShaderResourceView*, 6> inputs = {};		// t10..t15
			std::array<sg::UnorderedAccessView*, 8> outputs = {};	// u0..u7
		};

		Target create_target(sg::u32 width, sg::u32 height, DXGI_FORMAT format, sg::ResourceUsageFlags usage, sg::u32 mip_count = 1);
		BufferResource create_uav_buffer(sg::u32 element_size, sg::u32 element_count);
		void create_resources();
		void create_pipelines();
		sg::SharedPtr<sg::Pipeline> compute_pipeline(const char* shader);
		void bind(sg::CommandList& command_list, sg::PipelineType type, sg::ConstantBufferView& pass_cbv, const Binding& binding);
		void dispatch_screen(sg::CommandList& command_list, sg::Pipeline* pipeline, sg::ConstantBufferView& pass_cbv, const Binding& binding);

		void update_frame_constants(const Camera& camera);
		void update_sun();
		void update_exposure(float delta_time);
		void imgui(Camera& camera);

		void render_sky(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer);
		void render_geometry(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer);
		void render_geometry_phase(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer, sg::u32 phase);
		void build_hzb(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer);
		void render_lighting(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer);
		const Target& denoise(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer, DenoiseTargets& targets, const Target& raw, sg::u32 signal, int iterations, bool enabled);
		void render_path_tracer(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer);

		void begin_timer(sg::CommandList& command_list, const char* name);
		void end_timer(sg::CommandList& command_list);

		sg::SharedPtr<sg::Device> device;
		sg::SharedPtr<sg::CommandQueue> queue;
		sg::u32 width;
		sg::u32 height;
		bool supported = false;
		bool resources_created = false;
		std::unique_ptr<Scene> scene;
		sg::u32 scene_version = 0;		// Incremented when a scene becomes ready (resets histories)
		sg::u32 seen_scene_version = ~0u;
		// The radiance cache holds the old lighting after a preset switch, test settings or a cvar that resets history (sun,
		// lighting, materials): it is cleared rather than left to fade out over many frames
		bool clear_radiance_cache = false;
		sg::u32 cache_reset_count = 0;

		// Targets
		Target depth;
		Target visibility;
		Target hzb;
		std::vector<sg::UnorderedAccessView> hzb_mip_uavs;
		sg::u32 hzb_width = 1, hzb_height = 1, hzb_mips = 1;
		Target gbuffer_albedo;
		Target gbuffer_normal;
		Target gbuffer_geometry;
		Target gbuffer_emissive;
		Target motion;
		Target guide[2];
		Target shadow_raw;
		Target diffuse_raw;
		Target specular_raw;
		DenoiseTargets shadow_denoise;
		DenoiseTargets diffuse_denoise;
		DenoiseTargets specular_denoise;
		Target lit;
		Target taa_history[2];
		Target path_accumulation;
		Target path_output;
		Target sky;
		Target sky_multiple_scattering;
		Target dummy_texture;
		sg::u32 sky_width = 256, sky_height = 128;

		// Buffers
		BufferResource tasks;
		BufferResource task_counts;
		BufferResource draw_arguments;
		BufferResource instance_visibility;
		BufferResource meshlet_visibility;
		BufferResource cull_statistics;
		BufferResource histogram;
		BufferResource cache_keys;
		BufferResource cache_accumulators;
		BufferResource cache_values;
		BufferResource dummy_srv_buffer;
		BufferResource dummy_uav_buffer;
		sg::SharedPtr<sg::Buffer> statistics_readback;
		sg::SharedPtr<sg::Buffer> histogram_readback;
		sg::u32 cache_capacity = 1u << 20;
		sg::u32 task_capacity = 0;
		std::array<sg::u32, 4> task_region_offsets = {};
		std::array<sg::u32, 4> task_region_sizes = {};
		sg::SharedPtr<sg::IndirectArguments> indirect_arguments;

		// Pipelines
		sg::BindingDesc pass_binding_desc;
		struct Pipelines
		{
			sg::SharedPtr<sg::Pipeline> cull_instances;
			sg::SharedPtr<sg::Pipeline> build_draw_arguments;
			sg::SharedPtr<sg::Pipeline> visibility[PBR_BUCKET_COUNT];
			sg::SharedPtr<sg::Pipeline> hzb_first;
			sg::SharedPtr<sg::Pipeline> hzb_down;
			sg::SharedPtr<sg::Pipeline> resolve;
			sg::SharedPtr<sg::Pipeline> sky;
			sg::SharedPtr<sg::Pipeline> sky_multiple_scattering;
			sg::SharedPtr<sg::Pipeline> lighting;
			sg::SharedPtr<sg::Pipeline> cache_resolve;
			sg::SharedPtr<sg::Pipeline> denoise_temporal;
			sg::SharedPtr<sg::Pipeline> denoise_variance;
			sg::SharedPtr<sg::Pipeline> denoise_atrous;
			sg::SharedPtr<sg::Pipeline> composite;
			sg::SharedPtr<sg::Pipeline> taa;
			sg::SharedPtr<sg::Pipeline> histogram;
			sg::SharedPtr<sg::Pipeline> tonemap;
			sg::SharedPtr<sg::Pipeline> path_trace;
		} pipelines;

		// Per frame state
		gpu::FrameConstants frame = {};
		sg::ConstantBufferView frame_cbv;
		sg::u32 frame_index = 0;
		sg::u32 current = 0;			// Index of this frame's history targets (the other holds last frame's)
		bool history_valid = false;
		DirectX::XMFLOAT4X4 previous_view_projection = {};
		DirectX::XMFLOAT3 previous_camera_position = {};
		DirectX::XMFLOAT2 previous_jitter = {};
		DirectX::XMFLOAT4X4 culling_view_projection = {};
		DirectX::XMFLOAT4 culling_camera_position = {};
		std::array<DirectX::XMFLOAT4, 6> culling_planes = {};
		DirectX::XMFLOAT4X4 last_camera_matrix = {};
		sg::u32 last_reset_count = ~0u;
		sg::u32 accumulated_frames = 0;
		float exposure = 1.0f;
		float exposure_ev100 = 14.0f;
		bool exposure_initialised = false;
		bool frame_recorded = false;
		bool readback_valid = false;
		std::array<sg::u32, PBR_STAT_COUNT> cull_stats = {};
		DirectX::XMFLOAT3 sun_transmittance = { 1, 1, 1 };

		// GPU timings of each pass (last finished frame)
		sg::SharedPtr<sg::GPUTimestampPool> timestamp_pool;
		struct Timer
		{
			const char* name;
			sg::u32 index;
		};
		std::vector<Timer> timers;
		std::vector<std::pair<const char*, double>> timings;
		sg::u32 open_timer = ~0u;
		std::string pending_preset_load;

		// Camera track of the current scene (see get_camera_track) and its editor
		void update_camera_track();
		void camera_track_imgui(Camera& camera);
		gdr::CameraTrack camera_track;
		sg::u32 camera_track_version = 0;
		const ScenePreset* camera_track_preset = nullptr;
		std::string camera_track_path;				// Scene the track belongs to
		bool camera_track_needs_bounds = false;		// An orbit, made once the scene is ready
		float camera_track_target_distance = 5.0f;	// Editor: added points look this far ahead
		std::string camera_track_message;
		Camera* camera_on_ready = nullptr;	// Moved to the scene's view when the loading scene becomes ready	// Chosen in the window; loaded at the start of the next render
	};
}
