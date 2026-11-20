#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"
#include "sgVulkanMemory.h"
#include "sgVulkanShader.h"
#include "sgVulkanResourceState.h"
#include <mutex>
#include <string>
#include <unordered_map>

namespace sg
{
	class UploadHeap;
	namespace Vulkan
	{
		struct PipelineLayout;

		struct ImGuiTextureViewer
		{
			SharedPtr<Texture> texture;
			VkDescriptorSet descriptor_set = VK_NULL_HANDLE; // ImGui_ImplVulkan texture descriptor

			// Value to pass to ImGui::Image (as ImTextureID)
			u64 imgui_id() const { return reinterpret_cast<u64>(descriptor_set); }
		};

		class Device
		{
		public:
			Device();
			~Device();
			Device(const Device&) = delete;
			Device& operator=(const Device&) = delete;

			void imgui_init(u32 num_frames, DXGI_FORMAT format, DXGI_FORMAT depth_format, CommandQueue* command_queue);
			void imgui_update();
			void imgui_render(CommandList* command_list);
			void imgui_new_frame();
			void imgui_shutdown();
			u64 imgui_texture_viewer_id() const { return imgui_texture_viewer.imgui_id(); }
			ImGuiTextureViewer& imgui_texture_viewer_data() { return imgui_texture_viewer; }
			ImGuiTextureViewer allocate_imgui_texture_viewer_data() { return {}; }
			void set_imgui_texture_viewer_data(ImGuiTextureViewer& in_data, SharedPtr<Texture> texture);
			void set_imgui_viewer_texture(SharedPtr<Texture> texture);

			SizeAndAlignment calculate_resource_size_alignment(const ResourceCreateDesc& desc);

			SharedPtr<Memory> allocate_memory(MemoryType type, MemorySubType sub_type, u64 size, u64 alignment = DefaultAlignment::BUFFER_ALIGNMENT);
			SharedPtr<QueueFence> create_queue_fence();

			SharedPtr<GPUTimestampPool> create_gpu_timestamp_pool(CommandQueue* queue, u32 max_timestamps);
			SharedPtr<GPUStatisticPool> create_gpu_statistic_pool(CommandQueue* queue, u32 max_stats);

			SharedPtr<CommandQueue> create_command_queue();
			SharedPtr<CommandList> create_command_buffer();

			SharedPtr<VertexShader> create_vertex_shader(const std::vector<uint8_t>& shader);
			SharedPtr<PixelShader> create_pixel_shader(const std::vector<uint8_t>& shader);
			SharedPtr<ComputeShader> create_compute_shader(const std::vector<uint8_t>& shader);
			SharedPtr<MeshShader> create_mesh_shader(const std::vector<uint8_t>& shader);
			SharedPtr<AmplificationShader> create_amplification_shader(const std::vector<uint8_t>& shader);
			SharedPtr<GeometryShader> create_geometry_shader(const std::vector<uint8_t>& shader);

			//Returns current index to use
			u32 create_swap_chain(HWND hwnd, CommandQueue* command_queue, u32 buffer_count, DXGI_FORMAT format, u32 width, u32 height, SharedPtr<RenderTargetView>* rtv_list);
			u32 present_swap_chain(CommandQueue* command_queue);

			SharedPtr<Pipeline> create_pipeline(const PipelineDesc::Graphics& pipeline_desc, const BindingDesc& binding_desc);
			SharedPtr<Pipeline> create_pipeline(const PipelineDesc::Compute& pipeline_desc, const BindingDesc& binding_desc);
			SharedPtr<Pipeline> create_pipeline(const PipelineDesc::Mesh& pipeline_desc, const BindingDesc& binding_desc);

			SharedPtr<Buffer> create_buffer(SharedPtr<Memory> memory, u32 size, BufferType type, bool uav_access);
			SharedPtr<Texture> create_texture(SharedPtr<Memory> memory, u32 size, const ResourceCreateDesc& resource_desc);

			SharedPtr<Texture> create_texture_from_dds(const void* dds_memory, u64 dds_size, sg::UploadHeap& upload_heap);

			// Indirect
			SharedPtr<IndirectArguments> create_indirect_arguments();

			// DXGI_FORMAT_UNKNOWN means use default target format
			SharedPtr<RenderTargetView> create_render_target_view(SharedPtr<Texture>& texture, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);
			void free_render_target_view(RenderTargetView*) {} // Views own no descriptors in Vulkan

			// DXGI_FORMAT_UNKNOWN means use default target format
			SharedPtr<DepthStencilView> create_depth_stencil_view(SharedPtr<Texture>& texture, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);
			void free_depth_stencil_view(DepthStencilView*) {}

			ConstantBufferView create_constant_buffer_view(Buffer* buffer, u64 offset, u64 size);
			ShaderResourceView create_shader_resource_view(SharedPtr<Buffer> buffer, u64 element_size, u64 element_count);
			ShaderResourceView create_shader_resource_view(SharedPtr<Texture> texture, u32 top_mip = 0, u32 mip_count = static_cast<u32>(-1));
			UnorderedAccessView create_unordered_access_view(SharedPtr<Buffer> buffer, u64 element_size, u64 element_count);
			UnorderedAccessView create_unordered_access_view(SharedPtr<Texture> texture, u32 mip_level = 0);
			VertexBufferView create_vertex_buffer_view(SharedPtr<Buffer> buffer, u64 offset, u64 size, u64 stride);
			IndexBufferView create_index_buffer_view(SharedPtr<Buffer> buffer, u64 offset, u64 size, DXGI_FORMAT format);

			//Support checks
			bool SupportsMeshShaders() const { return mesh_shaders_supported; }
			size_t GetMeshShaderMaxNumThreads() { return 128; };
			size_t GetMeshShaderMaxOutputVerts() { return 256; };
			size_t GetMeshShaderMaxOutputPrims() { return 256; };

			bool SupportsWaveOps() const { return wave_ops_supported; }
			u32 GetWaveLaneCountMin() const { return wave_lane_count_min; }
			u32 GetWaveLaneCountMax() const { return wave_lane_count_max; }
			u32 GetTotalLaneCount() const { return 0; } // No Vulkan equivalent of D3D12's TotalLaneCount

			u32 GetFormatBitsPerUnit(DXGI_FORMAT format);
			u32 GetFormatWidthAlignment(DXGI_FORMAT format);
			u64 CalculateResourceSize(u32 width, u32 height, u32 depth, u32 mip_levels, DXGI_FORMAT format);

			void AddSamplers(const char* sampler_name, const sg::SamplerDesc& sampler_desc);
			void RecreateSamplers();

			// Vulkan specific
		public:
			const vk::raii::Device& get_device() const { return device; }
			const vk::raii::Queue& get_queue() const { return queue; }
			VmaAllocator get_allocator() const { return allocator; }
			vk::PipelineStageFlags2 get_shader_stages() const { return shader_stages; }
			bool supports_null_descriptors() const { return null_descriptors_supported; }
			vk::Sampler get_sampler(u32 index) const { return index < samplers.size() ? *samplers[index].sampler : vk::Sampler(); }

			// New images start in VK_IMAGE_LAYOUT_UNDEFINED. Their first transition (to their resting state) is recorded at the
			// start of the next queue submission, so it happens before any command that uses them.
			void add_pending_initial_transition(vk::Image image, vk::ImageAspectFlags aspect, ResourceState state);
			void remove_pending_initial_transition(vk::Image image);
			void take_pending_initial_transitions(std::vector<vk::ImageMemoryBarrier2>& out_barriers);

		private:
			void create_instance();
			void select_physical_device();
			void create_logical_device();
			SharedPtr<PipelineLayout> get_pipeline_layout(std::initializer_list<const Shader*> shaders);
			vk::raii::Sampler create_sampler(const sg::SamplerDesc& desc);

			// Swap chain
			void create_swap_chain_resources(vk::SwapchainKHR old_swap_chain);
			bool acquire_next_image();
			bool surface_has_area();
			void recreate_swap_chain();
			void enter_minimised_mode();

			vk::raii::Context context;
			vk::raii::Instance instance = nullptr;
			vk::raii::DebugUtilsMessengerEXT debug_messenger = nullptr;
			vk::raii::PhysicalDevice physical_device = nullptr;
			vk::raii::Device device = nullptr;
			vk::raii::Queue queue = nullptr;
			u32 queue_family = 0;
			VmaAllocator allocator = VK_NULL_HANDLE;
			vk::raii::PipelineCache pipeline_cache = nullptr;

			// Capabilities
			vk::PhysicalDeviceProperties properties;
			u32 max_push_descriptors = 0;
			bool mesh_shaders_supported = false;
			bool mesh_shader_queries_supported = false;
			bool null_descriptors_supported = false;
			bool memory_budget_supported = false;
			bool wave_ops_supported = false;
			u32 wave_lane_count_min = 0;
			u32 wave_lane_count_max = 0;
			vk::PipelineStageFlags2 shader_stages;
			// Memory types usable by any buffer / sampled+storage texture / render or depth target (see allocate_memory)
			u32 buffer_memory_type_bits = 0;
			u32 texture_memory_type_bits = 0;
			u32 target_memory_type_bits = 0;

			std::unordered_map<std::string, SharedPtr<PipelineLayout>> pipeline_layouts;

			struct SamplerInfo
			{
				std::string name;
				sg::SamplerDesc sampler_desc;
				vk::raii::Sampler sampler = nullptr;
			};
			std::vector<SamplerInfo> samplers;

			std::mutex pending_transitions_mutex;
			std::vector<vk::ImageMemoryBarrier2> pending_initial_transitions;

			// Swap chain
			HWND swap_chain_hwnd = nullptr;
			CommandQueue* present_queue = nullptr;
			vk::raii::SurfaceKHR surface = nullptr;
			vk::raii::SwapchainKHR swap_chain = nullptr;
			vk::SurfaceFormatKHR swap_chain_format;
			vk::Extent2D swap_chain_extent;
			u32 swap_chain_buffer_count = 0;
			u32 swap_chain_current_index = 0;
			u32 acquire_semaphore_index = 0;
			bool swap_chain_minimised = false;
			std::vector<SharedPtr<Texture>> swap_chain_textures;
			std::vector<bool> swap_chain_image_initialised;
			std::vector<vk::raii::Semaphore> acquire_semaphores;
			std::vector<vk::raii::Semaphore> present_semaphores;
			SharedPtr<Texture> minimised_back_buffer; // Rendered to instead of swap chain images while the window has no area

			// ImGui
			bool imgui_initialised = false;
			VkFormat imgui_colour_format = VK_FORMAT_UNDEFINED; // Referenced by ImGui's pipeline rendering info
			vk::raii::Sampler imgui_sampler = nullptr;
			ImGuiTextureViewer imgui_texture_viewer;
		};
	}
}
