#pragma once
#include <sgPlatformInclude.h>
#include "sgUploadHeap.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "pbrScene.h"
#include "pbrCameraTrack.h"
#include "pbrShaderStructures.h"
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

		// Hardware ray tracing and bindless resources are required (D3D12 only for now)
		bool is_supported() const { return supported; }

		// A preset name (scene_presets) or a file path. Presets also place the camera and the sun.
		bool load_scene(const std::string& name_or_path, Camera* camera);
		bool is_ready() const { return scene && scene->is_ready(); }
		// Places the camera at the loaded scene's starting view (the preset's, or the model file's camera)
		void apply_preset_camera(Camera& camera) const;
		const Scene* get_scene() const { return scene.get(); }
		// The scene's camera track (see SceneCameraTrack); the version changes whenever the track does
		const gdr::CameraTrack& get_camera_track() const { return camera_track.get(); }
		sg::u32 get_camera_track_version() const { return camera_track.get_version(); }

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
		// A pass's inputs (t10..t15) and outputs (u0..u7); unset slots get dummies. The scene is bound at t0..t8 and the sky
		// at t9, except for the passes that write the sky.
		struct Binding
		{
			std::array<sg::ShaderResourceView*, 6> inputs = {};
			std::array<sg::UnorderedAccessView*, 8> outputs = {};
			bool sky = true;
		};

		// Resources
		Target create_target(sg::u32 width, sg::u32 height, DXGI_FORMAT format, sg::ResourceUsageFlags usage, sg::u32 mip_count = 1);
		BufferResource create_buffer(sg::u32 element_size, sg::u32 element_count);
		void create_resources();
		void create_pipelines();
		void create_scene_resources();

		// Recording (valid while render() runs)
		template <typename T>
		sg::ConstantBufferView constants(const T& data) { return cbuffer->AllocateAndWrite(data); }
		void bind(sg::PipelineType type, sg::ConstantBufferView pass_constants, const Binding& binding);
		void dispatch(sg::Pipeline& pipeline, sg::ConstantBufferView pass_constants, const Binding& binding, sg::u32 groups_x, sg::u32 groups_y = 1);
		void dispatch_screen(sg::Pipeline& pipeline, sg::ConstantBufferView pass_constants, const Binding& binding);
		void clear(BufferResource& buffer, sg::u32 value = 0);
		struct ScopedTimer
		{
			Renderer& renderer;
			bool active;
			~ScopedTimer() { if (active) renderer.end_timer(); }
		};
		[[nodiscard]] ScopedTimer timed(const char* pass_name);
		void end_timer();

		// Per frame state
		void read_back_results(float delta_time);
		void update_exposure(float delta_time);
		void update_frame_constants(const Camera& camera);
		void update_sun();
		void reset_history() { history_reset_pending = true; }
		void imgui(Camera& camera);

		// Passes
		void render_sky();
		void render_geometry();
		void render_geometry_phase(sg::u32 phase);
		void build_hzb();
		void render_lighting();
		Target& denoise(DenoiseTargets& targets, Target& raw, sg::u32 signal, int iterations, bool enabled);
		void render_composite(const Target& shadow, const Target& diffuse, const Target& specular);
		Target& render_taa();
		Target& render_path_tracer();
		void render_tone_map(const Target* hdr, sg::UnorderedAccessView& output);

		sg::SharedPtr<sg::Device> device;
		sg::SharedPtr<sg::CommandQueue> queue;
		sg::u32 width;
		sg::u32 height;
		bool supported = false;
		bool resources_created = false;
		std::unique_ptr<Scene> scene;
		SceneCameraTrack camera_track;
		Camera* camera_on_ready = nullptr;	// Moved to the scene's view when the loading scene becomes ready
		std::string pending_preset_load;	// Chosen in the window; loaded at the start of the next render

		sg::CommandList* command_list = nullptr;
		SimpleLinearConstantBuffer* cbuffer = nullptr;

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
		static constexpr sg::u32 sky_width = 256, sky_height = 128;

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
		BufferResource dummy_srv_buffer;	// Separate dummies: a resource cannot be bound as an SRV and a UAV at once
		BufferResource dummy_uav_buffer;
		sg::SharedPtr<sg::Buffer> statistics_readback;
		sg::SharedPtr<sg::Buffer> histogram_readback;
		static constexpr sg::u32 cache_capacity = 1u << 20;
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

		// Frame state
		gpu::FrameConstants frame = {};
		sg::ConstantBufferView frame_cbv;
		sg::u32 frame_index = 0;
		sg::u32 current = 0;				// Index of this frame's history targets (the other holds last frame's)
		bool history_valid = false;			// Last frame's histories (denoiser, TAA) can be reprojected
		bool history_reset_pending = true;	// Histories, the radiance cache and the path tracer start over next frame
		sg::u32 seen_reset_count = ~0u;		// CVars::get_change_count(CVarResetHistory) when last seen
		DirectX::XMFLOAT4X4 previous_view_projection = {};
		DirectX::XMFLOAT4 culling_camera_position = {};
		std::array<DirectX::XMFLOAT4, 6> culling_planes = {};
		sg::u32 accumulated_frames = 0;
		float exposure = 1.0f;
		float exposure_ev100 = 14.0f;
		bool exposure_initialised = false;
		bool frame_recorded = false;
		bool readback_valid = false;
		std::array<sg::u32, PBR_STAT_COUNT> cull_stats = {};

		// GPU timings of each pass (averaged over recent frames)
		sg::SharedPtr<sg::GPUTimestampPool> timestamp_pool;
		struct Timer
		{
			const char* name;
			sg::u32 index;
		};
		std::vector<Timer> timers;
		std::vector<std::pair<const char*, double>> timings;
	};
}
