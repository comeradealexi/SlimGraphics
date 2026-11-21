#pragma once
#include <sgPlatformInclude.h>
#include "sgUploadHeap.h"
#include "pbrShaderStructures.h"
#include "LinearConstantBuffer.h"
#include <DirectXMath.h>
#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace pbr
{
	// A scene the viewer can open, with the view and sun it starts with
	struct ScenePreset
	{
		const char* name;
		const char* path;					// Relative to the working directory (SlimGraphicsApplication)
		float scale;						// Model units to metres
		DirectX::XMFLOAT3 camera_position;	// Metres (all zero with camera_target: the file's own camera)
		DirectX::XMFLOAT3 camera_target;
		float sun_elevation;				// Degrees
		float sun_azimuth;					// Degrees
		bool specular_is_orm;				// FBX "specular" textures hold occlusion / roughness / metallic (ORCA scenes)
		const char* camera_track;			// Camera track file (gdr::CameraTrack JSON, metres) or null: an orbit around the bounds
	};
	const std::vector<ScenePreset>& scene_presets();

	// A scene of meshes, materials and instances imported with assimp, uploaded for mesh shading (meshlets) and ray tracing
	// (one bottom level acceleration structure per mesh, one top level structure of every instance).
	// Loading runs over several frames: import (a worker thread), uploads within a per frame budget, a GPU pass that finds
	// alpha tested materials whose textures have cut outs, then acceleration structure builds.
	class Scene
	{
	public:
		enum class State { Empty, Importing, Uploading, AnalysingAlpha, BuildingAccelerationStructures, Ready, Failed };

		Scene(sg::SharedPtr<sg::Device> device);
		~Scene();

		// Starts loading (replacing the current scene); the preset may be null for an arbitrary file
		void load(const std::string& path, const ScenePreset* preset);
		// Advances loading: records uploads and GPU work into the command list
		void update(sg::CommandList& command_list, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer);

		State get_state() const { return state; }
		bool is_ready() const { return state == State::Ready; }
		std::string get_status() const;
		const std::string& get_path() const { return path; }
		const ScenePreset* get_preset() const { return preset; }
		// Switches to another preset of the same file (its camera and sun) without loading it again
		void set_preset(const ScenePreset* new_preset) { preset = new_preset; }

		struct Statistics
		{
			sg::u32 instances = 0;
			sg::u32 meshes = 0;
			sg::u32 meshlets = 0;				// Over all instances
			sg::u32 triangles = 0;				// Over all instances
			sg::u32 vertices = 0;				// Unique, in the vertex buffer
			sg::u32 materials = 0;
			sg::u32 textures = 0;
			sg::u32 alpha_tested_materials = 0;
			sg::u32 emissive_triangles = 0;
			sg::u64 texture_bytes = 0;
			sg::u64 acceleration_structure_bytes = 0;
			float import_seconds = 0.0f;
		};
		const Statistics& get_statistics() const { return statistics; }

		// Scene bounds (world space, metres)
		const DirectX::XMFLOAT3& get_bounds_min() const { return bounds_min; }
		const DirectX::XMFLOAT3& get_bounds_max() const { return bounds_max; }
		// The model file's first camera (world space, metres), when it has one
		bool get_file_camera(DirectX::XMFLOAT3& position, DirectX::XMFLOAT3& target) const
		{
			position = file_camera_position;
			target = file_camera_target;
			return has_file_camera;
		}

		// GPU data (valid when ready)
		struct Views
		{
			sg::ShaderResourceView vertices;
			sg::ShaderResourceView indices;
			sg::ShaderResourceView meshlets;
			sg::ShaderResourceView meshlet_vertices;
			sg::ShaderResourceView meshlet_triangles;
			sg::ShaderResourceView meshes;
			sg::ShaderResourceView instances;
			sg::ShaderResourceView materials;
			sg::ShaderResourceView tlas;
			sg::ShaderResourceView emissive_triangles;
		};
		Views& get_views() { return views; }
		sg::u32 get_instance_count() const { return statistics.instances; }
		sg::u32 get_meshlet_visibility_bits() const { return meshlet_visibility_bits; }
		// The largest number of amplification tasks (PBR_TASK_MESHLETS meshlet chunks) any frame can produce
		sg::u32 get_max_tasks() const { return max_tasks; }
		// Emissive triangle light sampling (gpu::EmissiveTriangle alias table)
		sg::u32 get_emissive_triangle_count() const { return emissive_triangle_count; }
		float get_emissive_weight() const { return emissive_weight; }

	private:
		struct ImportedScene;
		struct Texture;

		static std::shared_ptr<ImportedScene> import(std::string path, ScenePreset preset, bool has_preset);
		void begin_upload(sg::Ptr<sg::UploadHeap>& upload_heap);
		bool upload_textures(sg::Ptr<sg::UploadHeap>& upload_heap);
		void analyse_alpha(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer);
		void finish_alpha_analysis();
		void build_acceleration_structures(sg::CommandList& command_list, sg::Ptr<sg::UploadHeap>& upload_heap);
		void release();

		template <typename T>
		sg::SharedPtr<sg::Buffer> create_buffer(const std::vector<T>& data, sg::ShaderResourceView& srv);
		bool upload_pending_buffers(sg::Ptr<sg::UploadHeap>& upload_heap);

		sg::SharedPtr<sg::Device> device;
		State state = State::Empty;
		std::string path;
		const ScenePreset* preset = nullptr;
		std::future<std::shared_ptr<ImportedScene>> import_future;
		std::shared_ptr<ImportedScene> imported;
		std::string error;
		Statistics statistics;
		DirectX::XMFLOAT3 bounds_min = { 0, 0, 0 };
		DirectX::XMFLOAT3 bounds_max = { 0, 0, 0 };
		bool has_file_camera = false;
		DirectX::XMFLOAT3 file_camera_position = { 0, 0, 0 };
		DirectX::XMFLOAT3 file_camera_target = { 0, 0, 1 };

		Views views;
		sg::u32 meshlet_visibility_bits = 0;
		sg::u32 max_tasks = 0;
		sg::u32 emissive_triangle_count = 0;
		float emissive_weight = 0.0f;

		// Buffers whose data is uploaded in pieces within the per frame budget
		struct PendingUpload
		{
			sg::SharedPtr<sg::Buffer> buffer;
			std::shared_ptr<std::vector<uint8_t>> data;
			size_t offset = 0;
		};
		std::vector<PendingUpload> pending_uploads;
		std::vector<sg::SharedPtr<sg::Buffer>> buffers;	// Every scene buffer (kept alive)
		sg::SharedPtr<sg::Buffer> vertex_buffer;
		sg::SharedPtr<sg::Buffer> index_buffer;

		struct GPUTexture
		{
			sg::SharedPtr<sg::Texture> texture;
			sg::u32 bindless = ~0u;
		};
		std::vector<GPUTexture> textures;
		size_t next_texture = 0;

		std::vector<gpu::Material> materials;
		std::vector<gpu::Instance> instances;
		sg::SharedPtr<sg::Buffer> material_buffer;
		sg::SharedPtr<sg::Pipeline> alpha_analysis_pipeline;
		sg::SharedPtr<sg::Buffer> alpha_counts;
		sg::SharedPtr<sg::Buffer> alpha_readback;
		sg::UnorderedAccessView alpha_counts_uav;
		std::vector<sg::u32> alpha_candidates;			// Materials whose base colour alpha is analysed

		// Bottom level structures (one per mesh) packed into pool buffers
		struct BlasLocation
		{
			sg::u32 pool = 0;
			sg::u64 offset = 0;
		};
		std::vector<BlasLocation> blas;
		std::vector<sg::SharedPtr<sg::Buffer>> blas_pools;
		sg::u64 blas_pool_used = 0;
		sg::u64 blas_pool_capacity = 0;
		sg::SharedPtr<sg::Buffer> tlas;
		sg::SharedPtr<sg::Buffer> tlas_instances;
		sg::SharedPtr<sg::Buffer> scratch;
		size_t next_blas = 0;
	};
}
