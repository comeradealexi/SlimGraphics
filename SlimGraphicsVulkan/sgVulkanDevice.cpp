#include "pch.h"
#include "sgVulkanDevice.h"
#include "sgVulkanBuffer.h"
#include "sgVulkanCommandList.h"
#include "sgVulkanCommandQueue.h"
#include "sgVulkanConstantBufferView.h"
#include "sgVulkanDDS.h"
#include "sgVulkanDepthStencilView.h"
#include "sgVulkanGPUStatisticPool.h"
#include "sgVulkanGPUTimestampPool.h"
#include "sgVulkanIndexBufferView.h"
#include "sgVulkanPipeline.h"
#include "sgVulkanQueueFence.h"
#include "sgVulkanRenderTargetView.h"
#include "sgVulkanShaderResourceView.h"
#include "sgVulkanTexture.h"
#include "sgVulkanTypesTranslator.h"
#include "sgVulkanUnorderedAccessView.h"
#include "sgVulkanVertexBufferView.h"
#include "sgUploadHeap.h"
#include "sgUtils.h"
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <cstdio>

namespace sg
{
	namespace Vulkan
	{
		namespace
		{
			constexpr u32 kApiVersion = VK_API_VERSION_1_3;

			// Validation messages go to the debugger output. Set SG_VULKAN_VALIDATION_LOG=<path> to also append them to a file.
			VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
				const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void*)
			{
				const bool is_error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
				const char* level = is_error ? "ERROR" : "WARNING";
				seWriteLine("[Vulkan %s] %s", level, callback_data->pMessage);

				static FILE* log_file = []() -> FILE*
					{
						char path[MAX_PATH] = {};
						if (GetEnvironmentVariableA("SG_VULKAN_VALIDATION_LOG", path, MAX_PATH) == 0) return nullptr;
						FILE* f = nullptr;
						fopen_s(&f, path, "a");
						return f;
					}();
				if (log_file)
				{
					fprintf(log_file, "[%s] %s\n", level, callback_data->pMessage);
					fflush(log_file);
				}

				if (is_error && IsDebuggerPresent())
				{
					__debugbreak();
				}
				return VK_FALSE;
			}

			bool has_extension(const std::vector<vk::ExtensionProperties>& extensions, const char* name)
			{
				for (const vk::ExtensionProperties& e : extensions)
				{
					if (strcmp(e.extensionName, name) == 0) return true;
				}
				return false;
			}

			void check_imgui_vk_result(VkResult result)
			{
				seAssert(result == VK_SUCCESS, "ImGui Vulkan error %i", static_cast<int>(result));
			}

			// The Win32 platform backend does not create Vulkan surfaces, which ImGui needs for windows dragged outside the
			// main window (multi-viewports).
			int imgui_create_vk_surface(ImGuiViewport* viewport, ImU64 vk_instance, const void* vk_allocators, ImU64* out_vk_surface)
			{
				VkWin32SurfaceCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
				create_info.hwnd = static_cast<HWND>(viewport->PlatformHandleRaw ? viewport->PlatformHandleRaw : viewport->PlatformHandle);
				create_info.hinstance = GetModuleHandle(nullptr);
				VkSurfaceKHR surface = VK_NULL_HANDLE;
				const VkResult result = vkCreateWin32SurfaceKHR(reinterpret_cast<VkInstance>(vk_instance), &create_info, static_cast<const VkAllocationCallbacks*>(vk_allocators), &surface);
				*out_vk_surface = reinterpret_cast<ImU64>(surface);
				return static_cast<int>(result);
			}

			u32 find_vertex_input_location(const SpirvReflection& reflection, const InputLayout::ElementDesc& element, bool& found)
			{
				// DXC names inputs after the semantic as written in HLSL, e.g. "TEXCOORD0" or "POSITION"
				const std::string with_index = std::string(element.semantic_name) + std::to_string(element.semantic_index);
				for (const SpirvReflection::VertexInput& input : reflection.vertex_inputs)
				{
					if (_stricmp(input.semantic.c_str(), with_index.c_str()) == 0 || (element.semantic_index == 0 && _stricmp(input.semantic.c_str(), element.semantic_name) == 0))
					{
						found = true;
						return input.location;
					}
				}
				found = false;
				return 0;
			}

			vk::PipelineColorBlendAttachmentState translate_blend(const Blend::RenderTargetDesc& desc)
			{
				vk::PipelineColorBlendAttachmentState state;
				state.blendEnable = desc.blend_enable;
				state.srcColorBlendFactor = translate(desc.src_blend);
				state.dstColorBlendFactor = translate(desc.dest_blend);
				state.colorBlendOp = translate(desc.blend_op);
				state.srcAlphaBlendFactor = translate(desc.src_blend_alpha);
				state.dstAlphaBlendFactor = translate(desc.dest_blend_alpha);
				state.alphaBlendOp = translate(desc.blend_op_alpha);
				state.colorWriteMask = static_cast<vk::ColorComponentFlags>(desc.render_target_write_mask & 0xf);
				return state;
			}

			vk::StencilOpState translate_stencil(const DepthStencil::StencilDesc& desc, const DepthStencil::Desc& ds)
			{
				return vk::StencilOpState(translate(desc.fail_operation), translate(desc.pass_operation), translate(desc.depth_operation),
					translate(desc.comparison_operation), ds.stencil_read_mask, ds.stencil_write_mask, 0);
			}
		}

		Device::Device()
		{
			seWriteLine("Creating Vulkan sg::Device...");
			create_instance();
			select_physical_device();
			create_logical_device();

			// Memory allocator
			{
				VmaAllocatorCreateInfo create_info = {};
				create_info.vulkanApiVersion = kApiVersion;
				create_info.instance = static_cast<VkInstance>(*instance);
				create_info.physicalDevice = static_cast<VkPhysicalDevice>(*physical_device);
				create_info.device = static_cast<VkDevice>(*device);
				create_info.flags = memory_budget_supported ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT : 0;
				CHECKVK(vmaCreateAllocator(&create_info, &allocator));
			}

			// Memory types every buffer / texture / target can be bound to. Resources are bound into memory allocated before
			// they are created (as with D3D12 placed resources), so the allocation must use a type compatible with all of them.
			{
				const vk::BufferUsageFlags buffer_usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
					vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
					vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndirectBuffer;
				const vk::BufferCreateInfo buffer_info({}, 64 * 1024, buffer_usage);
				buffer_memory_type_bits = device.getBufferMemoryRequirements(vk::DeviceBufferMemoryRequirements(&buffer_info)).memoryRequirements.memoryTypeBits;

				auto image_type_bits = [this](vk::Format format, vk::ImageUsageFlags usage)
					{
						const vk::ImageCreateInfo image_info({}, vk::ImageType::e2D, format, vk::Extent3D(256, 256, 1), 1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, usage);
						return device.getImageMemoryRequirements(vk::DeviceImageMemoryRequirements(&image_info)).memoryRequirements.memoryTypeBits;
					};
				const vk::ImageUsageFlags transfer_sampled = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
				texture_memory_type_bits = image_type_bits(vk::Format::eR8G8B8A8Unorm, transfer_sampled | vk::ImageUsageFlagBits::eStorage) &
					image_type_bits(vk::Format::eBc1RgbaUnormBlock, transfer_sampled) & image_type_bits(vk::Format::eR32Sfloat, transfer_sampled | vk::ImageUsageFlagBits::eStorage);
				target_memory_type_bits = image_type_bits(vk::Format::eR8G8B8A8Unorm, transfer_sampled | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage) &
					image_type_bits(vk::Format::eD32Sfloat, transfer_sampled | vk::ImageUsageFlagBits::eDepthStencilAttachment);
			}

			pipeline_cache = vk::raii::PipelineCache(device, vk::PipelineCacheCreateInfo());

			// Sampler used to display textures through ImGui
			imgui_sampler = vk::raii::Sampler(device, vk::SamplerCreateInfo({}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
				vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, 0.0f, VK_FALSE, 1.0f, VK_FALSE, vk::CompareOp::eNever, 0.0f, VK_LOD_CLAMP_NONE));

			seWriteLine("Vulkan device created: %s", properties.deviceName.data());
		}

		Device::~Device()
		{
			device.waitIdle();

			// Release everything that owns VMA memory or device objects before the allocator and device are destroyed
			imgui_texture_viewer = {};
			minimised_back_buffer = nullptr;
			swap_chain_textures.clear();
			pipeline_layouts.clear();
			samplers.clear();
			if (allocator)
			{
				vmaDestroyAllocator(allocator);
				allocator = VK_NULL_HANDLE;
			}
		}

		void Device::create_instance()
		{
			const std::vector<vk::ExtensionProperties> available_extensions = context.enumerateInstanceExtensionProperties();
			const std::vector<vk::LayerProperties> available_layers = context.enumerateInstanceLayerProperties();

			std::vector<const char*> extensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
			std::vector<const char*> layers;
#if defined(_DEBUG)
			const bool debug_utils = has_extension(available_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			if (debug_utils)
			{
				extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			}
			for (const vk::LayerProperties& layer : available_layers)
			{
				if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
				{
					layers.push_back("VK_LAYER_KHRONOS_validation");
					break;
				}
			}
#endif

			const vk::ApplicationInfo app_info("SlimGraphics", 1, "SlimGraphics", 1, kApiVersion);
			instance = vk::raii::Instance(context, vk::InstanceCreateInfo({}, &app_info, layers, extensions));

#if defined(_DEBUG)
			if (debug_utils)
			{
				const vk::DebugUtilsMessengerCreateInfoEXT messenger_info({},
					vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
					vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
					reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(&vulkan_debug_callback));
				debug_messenger = vk::raii::DebugUtilsMessengerEXT(instance, messenger_info);
			}
#endif
		}

		void Device::select_physical_device()
		{
			vk::raii::PhysicalDevices physical_devices(instance);
			seAssert(!physical_devices.empty(), "No Vulkan physical devices found");

			// Prefer a discrete GPU supporting Vulkan 1.3 with a graphics queue that can present to Win32 windows
			int best_score = -1;
			for (vk::raii::PhysicalDevice& candidate : physical_devices)
			{
				const vk::PhysicalDeviceProperties candidate_properties = candidate.getProperties();
				seWriteLine("Found Vulkan physical device: %s", candidate_properties.deviceName.data());
				if (candidate_properties.apiVersion < kApiVersion)
				{
					continue;
				}
				const std::vector<vk::QueueFamilyProperties> families = candidate.getQueueFamilyProperties();
				for (u32 family = 0; family < families.size(); family++)
				{
					const vk::QueueFlags required = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
					if ((families[family].queueFlags & required) != required || !candidate.getWin32PresentationSupportKHR(family))
					{
						continue;
					}
					const int score = candidate_properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu ? 2 : (candidate_properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu ? 1 : 0);
					if (score > best_score)
					{
						best_score = score;
						physical_device = vk::raii::PhysicalDevice(instance, *candidate);
						queue_family = family;
					}
					break;
				}
			}

			seAssert(best_score >= 0, "No Vulkan 1.3 device with a graphics + present queue found");
			if (best_score < 0)
			{
				MessageBoxA(nullptr, "No Vulkan 1.3 capable GPU was found.", "Slim Graphics", MB_OK | MB_ICONERROR);
				ExitProcess(1);
			}
			properties = physical_device.getProperties();
		}

		void Device::create_logical_device()
		{
			const std::vector<vk::ExtensionProperties> available = physical_device.enumerateDeviceExtensionProperties();

			auto supported_features = physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features,
				vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceMeshShaderFeaturesEXT, vk::PhysicalDeviceRobustness2FeaturesEXT>();
			const vk::PhysicalDeviceFeatures& core = supported_features.get<vk::PhysicalDeviceFeatures2>().features;
			const vk::PhysicalDeviceVulkan12Features& core12 = supported_features.get<vk::PhysicalDeviceVulkan12Features>();
			const vk::PhysicalDeviceVulkan13Features& core13 = supported_features.get<vk::PhysicalDeviceVulkan13Features>();
			const vk::PhysicalDeviceMeshShaderFeaturesEXT& mesh = supported_features.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>();
			const vk::PhysicalDeviceRobustness2FeaturesEXT& robustness2 = supported_features.get<vk::PhysicalDeviceRobustness2FeaturesEXT>();

			seAssert(core13.dynamicRendering && core13.synchronization2 && core12.timelineSemaphore && core12.hostQueryReset, "Required Vulkan 1.2/1.3 features missing");
			seAssert(has_extension(available, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME), "VK_KHR_push_descriptor is required");

			std::vector<const char*> extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME };
			// Enabled explicitly (although core in 1.3) because the ImGui backend loads the KHR entry points for secondary viewports
			if (has_extension(available, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))
			{
				extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
			}
			mesh_shaders_supported = has_extension(available, VK_EXT_MESH_SHADER_EXTENSION_NAME) && mesh.meshShader && mesh.taskShader;
			if (mesh_shaders_supported)
			{
				extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
			}
			const bool robustness2_ext = has_extension(available, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
			const bool robustness2_khr = has_extension(available, VK_KHR_ROBUSTNESS_2_EXTENSION_NAME);
			null_descriptors_supported = (robustness2_ext || robustness2_khr) && robustness2.nullDescriptor;
			if (null_descriptors_supported)
			{
				extensions.push_back(robustness2_khr ? VK_KHR_ROBUSTNESS_2_EXTENSION_NAME : VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
			}
			memory_budget_supported = has_extension(available, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
			if (memory_budget_supported)
			{
				extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
			}

			// Enable only what the renderer uses
			vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features,
				vk::PhysicalDeviceMeshShaderFeaturesEXT, vk::PhysicalDeviceRobustness2FeaturesEXT> enabled;
			vk::PhysicalDeviceFeatures& f = enabled.get<vk::PhysicalDeviceFeatures2>().features;
			f.geometryShader = core.geometryShader;
			f.fillModeNonSolid = core.fillModeNonSolid;
			f.samplerAnisotropy = core.samplerAnisotropy;
			f.pipelineStatisticsQuery = core.pipelineStatisticsQuery;
			f.logicOp = core.logicOp;
			f.dualSrcBlend = core.dualSrcBlend;
			f.independentBlend = core.independentBlend;
			f.fragmentStoresAndAtomics = core.fragmentStoresAndAtomics;
			f.vertexPipelineStoresAndAtomics = core.vertexPipelineStoresAndAtomics;
			f.shaderStorageImageReadWithoutFormat = core.shaderStorageImageReadWithoutFormat;
			f.shaderStorageImageWriteWithoutFormat = core.shaderStorageImageWriteWithoutFormat;
			f.depthClamp = core.depthClamp;
			f.shaderInt16 = core.shaderInt16;
			f.shaderInt64 = core.shaderInt64;
			enabled.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = supported_features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters;
			vk::PhysicalDeviceVulkan12Features& e12 = enabled.get<vk::PhysicalDeviceVulkan12Features>();
			e12.timelineSemaphore = VK_TRUE;
			e12.hostQueryReset = VK_TRUE;
			e12.shaderFloat16 = core12.shaderFloat16;
			e12.storageBuffer8BitAccess = core12.storageBuffer8BitAccess;
			e12.uniformAndStorageBuffer8BitAccess = core12.uniformAndStorageBuffer8BitAccess;
			e12.shaderInt8 = core12.shaderInt8;
			e12.vulkanMemoryModel = core12.vulkanMemoryModel;
			e12.vulkanMemoryModelDeviceScope = core12.vulkanMemoryModelDeviceScope;
			vk::PhysicalDeviceVulkan13Features& e13 = enabled.get<vk::PhysicalDeviceVulkan13Features>();
			e13.dynamicRendering = VK_TRUE;
			e13.synchronization2 = VK_TRUE;
			e13.maintenance4 = core13.maintenance4;
			e13.subgroupSizeControl = core13.subgroupSizeControl;
			e13.shaderDemoteToHelperInvocation = core13.shaderDemoteToHelperInvocation;
			vk::PhysicalDeviceMeshShaderFeaturesEXT& emesh = enabled.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>();
			if (mesh_shaders_supported)
			{
				emesh.meshShader = VK_TRUE;
				emesh.taskShader = VK_TRUE;
				emesh.meshShaderQueries = mesh.meshShaderQueries && core.pipelineStatisticsQuery;
				mesh_shader_queries_supported = emesh.meshShaderQueries;
			}
			else
			{
				enabled.unlink<vk::PhysicalDeviceMeshShaderFeaturesEXT>();
			}
			if (null_descriptors_supported)
			{
				enabled.get<vk::PhysicalDeviceRobustness2FeaturesEXT>().nullDescriptor = VK_TRUE;
			}
			else
			{
				enabled.unlink<vk::PhysicalDeviceRobustness2FeaturesEXT>();
			}

			const float queue_priority = 1.0f;
			const vk::DeviceQueueCreateInfo queue_create_info({}, queue_family, 1, &queue_priority);
			vk::DeviceCreateInfo device_create_info({}, queue_create_info, {}, extensions);
			device_create_info.pNext = &enabled.get<vk::PhysicalDeviceFeatures2>();
			device = vk::raii::Device(physical_device, device_create_info);

			// Unlike D3D12, queues are created with the device
			queue = vk::raii::Queue(device, queue_family, 0);

			// Capabilities used elsewhere
			auto props = physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDevicePushDescriptorPropertiesKHR, vk::PhysicalDeviceSubgroupProperties, vk::PhysicalDeviceSubgroupSizeControlProperties>();
			max_push_descriptors = props.get<vk::PhysicalDevicePushDescriptorPropertiesKHR>().maxPushDescriptors;
			const vk::PhysicalDeviceSubgroupProperties& subgroup = props.get<vk::PhysicalDeviceSubgroupProperties>();
			const vk::PhysicalDeviceSubgroupSizeControlProperties& subgroup_size = props.get<vk::PhysicalDeviceSubgroupSizeControlProperties>();
			const vk::SubgroupFeatureFlags wave_ops = vk::SubgroupFeatureFlagBits::eBasic | vk::SubgroupFeatureFlagBits::eVote | vk::SubgroupFeatureFlagBits::eArithmetic | vk::SubgroupFeatureFlagBits::eBallot;
			wave_ops_supported = (subgroup.supportedOperations & wave_ops) == wave_ops && (subgroup.supportedStages & vk::ShaderStageFlagBits::eFragment) && (subgroup.supportedStages & vk::ShaderStageFlagBits::eCompute);
			wave_lane_count_min = core13.subgroupSizeControl ? subgroup_size.minSubgroupSize : subgroup.subgroupSize;
			wave_lane_count_max = core13.subgroupSizeControl ? subgroup_size.maxSubgroupSize : subgroup.subgroupSize;

			using S = vk::PipelineStageFlagBits2;
			shader_stages = S::eVertexShader | S::eFragmentShader | S::eComputeShader;
			if (f.geometryShader) shader_stages |= S::eGeometryShader;
			if (mesh_shaders_supported) shader_stages |= S::eTaskShaderEXT | S::eMeshShaderEXT;
		}

		void Device::imgui_init(u32 num_frames, DXGI_FORMAT format, DXGI_FORMAT depth_format, CommandQueue* command_queue)
		{
			seAssert(swap_chain_buffer_count > 0, "Create the swap chain before initialising ImGui (its format is needed)");
			imgui_colour_format = static_cast<VkFormat>(swap_chain_format.format);

			ImGui_ImplVulkan_InitInfo init_info = {};
			init_info.ApiVersion = kApiVersion;
			init_info.Instance = static_cast<VkInstance>(*instance);
			init_info.PhysicalDevice = static_cast<VkPhysicalDevice>(*physical_device);
			init_info.Device = static_cast<VkDevice>(*device);
			init_info.QueueFamily = queue_family;
			init_info.Queue = static_cast<VkQueue>(*queue);
			init_info.DescriptorPoolSize = 64; // ImGui owned pool: font atlas + textures shown with ImGui::Image
			init_info.MinImageCount = 2;
			init_info.ImageCount = std::max(2u, swap_chain_buffer_count);
			init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
			init_info.PipelineCache = static_cast<VkPipelineCache>(*pipeline_cache);
			init_info.UseDynamicRendering = true;
			init_info.PipelineRenderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
			init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
			init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &imgui_colour_format;
			init_info.CheckVkResultFn = check_imgui_vk_result;
			ImGui::GetPlatformIO().Platform_CreateVkSurface = imgui_create_vk_surface;
			const bool ok = ImGui_ImplVulkan_Init(&init_info);
			seAssert(ok, "ImGui_ImplVulkan_Init failed");
			(void)ok;
			imgui_initialised = true;

			// Default viewer texture (replaced by the magnifying glass target), matching the D3D12 backend
			ResourceCreateDesc rcd(1024, 1024, DXGI_FORMAT_R8G8B8A8_UNORM);
			const SizeAndAlignment size_align = calculate_resource_size_alignment(rcd);
			SharedPtr<Memory> memory = allocate_memory(MemoryType::GPUOptimal, MemorySubType::Texture, size_align.size, size_align.alignment);
			set_imgui_viewer_texture(create_texture(memory, static_cast<u32>(size_align.size), rcd));
		}

		void Device::imgui_new_frame()
		{
			ImGui_ImplVulkan_NewFrame();
		}

		void Device::imgui_shutdown()
		{
			if (!imgui_initialised)
			{
				return;
			}
			device.waitIdle();
			imgui_texture_viewer = {}; // Descriptor sets are freed with the ImGui descriptor pool
			ImGui_ImplVulkan_Shutdown();
			imgui_initialised = false;
		}

		void Device::imgui_update()
		{
			ImGui::PushID("SGDevice");

			if (ImGui::CollapsingHeader("Device"))
			{
				ImGui::Text("Adapter ID: %s", properties.deviceName.data());
				ImGui::Text("API: Vulkan %u.%u.%u", VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion));
				const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
				u64 device_local = 0;
				u64 host = 0;
				for (u32 i = 0; i < memory_properties.memoryHeapCount; i++)
				{
					if (memory_properties.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) device_local += memory_properties.memoryHeaps[i].size;
					else host += memory_properties.memoryHeaps[i].size;
				}
				ImGui::Text("Dedicated Video Memory: %llu (MiB)", device_local / 1024llu / 1024llu);
				ImGui::Text("Shared System Memory: %llu (MiB)", host / 1024llu / 1024llu);
				ImGui::SeparatorText("Feature Support");
				ImGui::BeginDisabled();
				ImGui::RadioButton("UMA", properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu);
				ImGui::RadioButton("Mesh Shaders", SupportsMeshShaders());
				ImGui::RadioButton("Wave Operations", SupportsWaveOps());
				ImGui::RadioButton("Null Descriptors", null_descriptors_supported);
				ImGui::Text("Wave Lane Count Min: %u", GetWaveLaneCountMin());
				ImGui::Text("Wave Lane Count Max: %u", GetWaveLaneCountMax());
				ImGui::Text("Max Push Descriptors: %u", max_push_descriptors);
				ImGui::EndDisabled();
			}
			if (ImGui::CollapsingHeader("Memory"))
			{
				const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
				std::vector<VmaBudget> budgets(memory_properties.memoryHeapCount);
				vmaGetHeapBudgets(allocator, budgets.data());
				ImGui::SeparatorText("Memory Heaps (MiB):");
				for (u32 i = 0; i < memory_properties.memoryHeapCount; i++)
				{
					const bool local = (memory_properties.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) != vk::MemoryHeapFlags();
					ImGui::Text("Heap %u (%s): Usage %llu / Budget %llu - Allocated %llu", i, local ? "device local" : "host",
						budgets[i].usage / 1024llu / 1024llu, budgets[i].budget / 1024llu / 1024llu, budgets[i].statistics.allocationBytes / 1024llu / 1024llu);
				}
				ImGui::Indent();
				if (ImGui::CollapsingHeader("Detailed Memory Information"))
				{
					ImGui::Text("Viewing this information is slow to be calculated!");
					VmaTotalStatistics total = {};
					vmaCalculateStatistics(allocator, &total);
					for (u32 i = 0; i < memory_properties.memoryTypeCount; i++)
					{
						const VmaDetailedStatistics& stats = total.memoryType[i];
						if (stats.statistics.blockCount == 0) continue;
						ImGui::SeparatorText(("Memory Type " + std::to_string(i)).c_str());
						ImGui::Text("- Memory (KiB): %llu/%llu", stats.statistics.allocationBytes / 1024llu, stats.statistics.blockBytes / 1024llu);
						ImGui::Text("- Block Count: %u AllocationCount: %u", stats.statistics.blockCount, stats.statistics.allocationCount);
						ImGui::Text("- AllocationSizeMin (KiB): %llu AllocationSizeMax (KiB): %llu", stats.allocationSizeMin / 1024llu, stats.allocationSizeMax / 1024llu);
					}
				}
				ImGui::Unindent();
			}

			if (ImGui::CollapsingHeader("Global Samplers"))
			{
				ImGui::Indent();
				bool samplers_modified = false;
				ImGui::PushID("Samplers");
				for (size_t i = 0; i < samplers.size(); i++)
				{
					ImGui::PushID(static_cast<int>(i));
					SamplerDesc& desc = samplers[i].sampler_desc;
					if (ImGui::CollapsingHeader(samplers[i].name.c_str()))
					{
						static const char* filters[] = { "MinMagMipPoint","MinMagPointMipLinear","MinPointMagLinearMipPoint","MinPointMagMipLinear","MinLinearMagMipPoint","MinLinearMagPointMipLinear","MinMagLinearMipPoint","MinMagMipLinear","MinMagAnisotropicMipPoint","Anisotropic" };
						static const char* texture_address[] = { "Wrap","Mirror","Clamp","Border","MirrorOnce" };
						samplers_modified |= ImGui::Combo("Filter", (int*)&desc.filter, filters, IM_ARRAYSIZE(filters));
						samplers_modified |= ImGui::Combo("AddressU", (int*)&desc.address_u, texture_address, IM_ARRAYSIZE(texture_address));
						samplers_modified |= ImGui::Combo("AddressV", (int*)&desc.address_v, texture_address, IM_ARRAYSIZE(texture_address));
						samplers_modified |= ImGui::Combo("AddressW", (int*)&desc.address_w, texture_address, IM_ARRAYSIZE(texture_address));
						samplers_modified |= ImGui::SliderFloat("Mip Lod Bias", &desc.mip_lod_bias, -16.0f, 15.990000f);
						samplers_modified |= ImGui::SliderInt("Max Anisotropy", (int*)&desc.max_anisotropy, 1, 16);
						samplers_modified |= ImGui::SliderFloat("Min Lod", &desc.min_lod, 0.0f, 16.0f);
						samplers_modified |= ImGui::SliderFloat("Max Lod", &desc.max_lod, 0.0f, 16.0f);
						if (desc.max_lod < desc.min_lod) desc.min_lod = desc.max_lod;
					}
					ImGui::PopID();
				}
				if (samplers_modified)
				{
					RecreateSamplers();
				}
				ImGui::PopID();
				ImGui::Unindent();
			}

			ImGui::PopID();
		}

		void Device::imgui_render(CommandList* command_list)
		{
			// ImGui records into our command buffer, so dynamic rendering must be active, and it changes bound state
			command_list->ensure_rendering();
			ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_list->get_command_buffer());
			command_list->invalidate_state();

#ifdef SE_IMGUI_DOCKING
			// Update and Render additional Platform Windows (ImGui records and submits these itself)
			if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				ImGui::UpdatePlatformWindows();
				ImGui::RenderPlatformWindowsDefault();
			}
#endif
		}

		void Device::set_imgui_texture_viewer_data(ImGuiTextureViewer& in_data, SharedPtr<Texture> texture)
		{
			if (in_data.descriptor_set)
			{
				ImGui_ImplVulkan_RemoveTexture(in_data.descriptor_set);
			}
			in_data.texture = texture;
			in_data.descriptor_set = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*imgui_sampler), static_cast<VkImageView>(texture->get_shader_resource_view()),
				VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
		}

		void Device::set_imgui_viewer_texture(SharedPtr<Texture> texture)
		{
			set_imgui_texture_viewer_data(imgui_texture_viewer, texture);
		}

		SizeAndAlignment Device::calculate_resource_size_alignment(const ResourceCreateDesc& desc)
		{
			if (desc.dimension == ResourceDimension::Buffer)
			{
				const vk::BufferCreateInfo buffer_info({}, desc.width, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
				const vk::MemoryRequirements req = device.getBufferMemoryRequirements(vk::DeviceBufferMemoryRequirements(&buffer_info)).memoryRequirements;
				return { req.size, req.alignment };
			}
			ResourceCreateDesc image_desc = desc;
			vk::ImageCreateInfo image_info;
			image_info.imageType = desc.dimension == ResourceDimension::Texture1D ? vk::ImageType::e1D : (desc.dimension == ResourceDimension::Texture3D ? vk::ImageType::e3D : vk::ImageType::e2D);
			image_info.format = translate(image_desc.format);
			image_info.extent = vk::Extent3D(desc.width, desc.height, desc.dimension == ResourceDimension::Texture3D ? desc.depth : 1);
			image_info.mipLevels = std::max(1u, desc.mip_count);
			image_info.arrayLayers = desc.dimension == ResourceDimension::Texture3D ? 1 : std::max(1u, desc.depth);
			image_info.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
			if ((desc.usage_flags & ResourceUsageFlags::RenderTarget) == ResourceUsageFlags::RenderTarget) image_info.usage |= vk::ImageUsageFlagBits::eColorAttachment;
			if ((desc.usage_flags & ResourceUsageFlags::DepthStencil) == ResourceUsageFlags::DepthStencil) image_info.usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
			if ((desc.usage_flags & ResourceUsageFlags::UnorderedAccess) == ResourceUsageFlags::UnorderedAccess) image_info.usage |= vk::ImageUsageFlagBits::eStorage;
			const vk::MemoryRequirements req = device.getImageMemoryRequirements(vk::DeviceImageMemoryRequirements(&image_info)).memoryRequirements;
			return { req.size, req.alignment };
		}

		SharedPtr<Memory> Device::allocate_memory(MemoryType type, MemorySubType sub_type, u64 size, u64 alignment)
		{
			VkMemoryRequirements requirements = {};
			requirements.alignment = std::max<u64>(alignment, 1);
			requirements.size = AlignUp(size, requirements.alignment);
			switch (sub_type)
			{
			case MemorySubType::Texture: requirements.memoryTypeBits = texture_memory_type_bits; break;
			case MemorySubType::Target: requirements.memoryTypeBits = target_memory_type_bits; break;
			default: requirements.memoryTypeBits = buffer_memory_type_bits; break;
			}

			VmaAllocationCreateInfo create_info = {};
			switch (type)
			{
			case MemoryType::Upload:
				create_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
				create_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
				break;
			case MemoryType::Readback:
				create_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
				create_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
				create_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
				break;
			case MemoryType::GPUOptimal:
				create_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
				break;
			}

			VmaAllocation allocation = VK_NULL_HANDLE;
			CHECKVK(vmaAllocateMemory(allocator, &requirements, &create_info, &allocation, nullptr));
			return SharedPtr<Memory>(new Memory(type, allocator, allocation));
		}

		SharedPtr<QueueFence> Device::create_queue_fence()
		{
			SharedPtr<QueueFence> fence(new QueueFence());
			vk::SemaphoreTypeCreateInfo timeline_info(vk::SemaphoreType::eTimeline, 0);
			fence->timeline_semaphore = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo({}, &timeline_info));
			return fence;
		}

		SharedPtr<GPUTimestampPool> Device::create_gpu_timestamp_pool(CommandQueue* command_queue, u32 max_timestamps)
		{
			const std::vector<vk::QueueFamilyProperties> families = physical_device.getQueueFamilyProperties();
			const u32 valid_bits = families[queue_family].timestampValidBits;
			seAssert(valid_bits > 0, "Queue does not support timestamps");
			return SharedPtr<GPUTimestampPool>(new GPUTimestampPool(device, properties.limits.timestampPeriod, valid_bits, max_timestamps));
		}

		SharedPtr<GPUStatisticPool> Device::create_gpu_statistic_pool(CommandQueue* command_queue, u32 max_stats)
		{
			return SharedPtr<GPUStatisticPool>(new GPUStatisticPool(device, mesh_shader_queries_supported, max_stats));
		}

		SharedPtr<CommandQueue> Device::create_command_queue()
		{
			return SharedPtr<CommandQueue>(new CommandQueue(*this, *queue, queue_family));
		}

		SharedPtr<CommandList> Device::create_command_buffer()
		{
			return SharedPtr<CommandList>(new CommandList(*this, queue_family));
		}

		SharedPtr<VertexShader> Device::create_vertex_shader(const std::vector<uint8_t>& shader)
		{
			return SharedPtr<VertexShader>(new VertexShader(device, shader));
		}

		SharedPtr<PixelShader> Device::create_pixel_shader(const std::vector<uint8_t>& shader)
		{
			return SharedPtr<PixelShader>(new PixelShader(device, shader));
		}

		SharedPtr<ComputeShader> Device::create_compute_shader(const std::vector<uint8_t>& shader)
		{
			return SharedPtr<ComputeShader>(new ComputeShader(device, shader));
		}

		SharedPtr<MeshShader> Device::create_mesh_shader(const std::vector<uint8_t>& shader)
		{
			if (!mesh_shaders_supported)
			{
				return nullptr; // The module would use SPV_EXT_mesh_shader capabilities the device does not have
			}
			return SharedPtr<MeshShader>(new MeshShader(device, shader));
		}

		SharedPtr<AmplificationShader> Device::create_amplification_shader(const std::vector<uint8_t>& shader)
		{
			if (!mesh_shaders_supported)
			{
				return nullptr;
			}
			return SharedPtr<AmplificationShader>(new AmplificationShader(device, shader));
		}

		SharedPtr<GeometryShader> Device::create_geometry_shader(const std::vector<uint8_t>& shader)
		{
			return SharedPtr<GeometryShader>(new GeometryShader(device, shader));
		}

		SharedPtr<PipelineLayout> Device::get_pipeline_layout(std::initializer_list<const Shader*> shaders)
		{
			// Union of the bindings used by all stages
			std::vector<PipelineLayout::Binding> bindings;
			for (const Shader* shader : shaders)
			{
				if (!shader) continue;
				const vk::ShaderStageFlags stage = shader->get_stage();
				for (const SpirvReflection::DescriptorBinding& b : shader->get_reflection().bindings)
				{
					seAssert(b.set == 0 && b.binding < BindingShift::END && b.count == 1, "Unsupported shader binding (set %u binding %u count %u)", b.set, b.binding, b.count);
					auto it = std::find_if(bindings.begin(), bindings.end(), [&b](const PipelineLayout::Binding& e) { return e.binding == b.binding; });
					if (it != bindings.end())
					{
						seAssert(it->type == b.type, "Binding %u used with different descriptor types across stages", b.binding);
						it->stages |= stage;
					}
					else
					{
						bindings.push_back({ b.binding, b.type, stage });
					}
				}
			}
			std::sort(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) { return a.binding < b.binding; });
			seAssert(bindings.size() <= max_push_descriptors, "Pipeline uses %u descriptors, more than maxPushDescriptors (%u)", static_cast<u32>(bindings.size()), max_push_descriptors);

			std::string key;
			for (const PipelineLayout::Binding& b : bindings)
			{
				key += std::to_string(b.binding) + ":" + std::to_string(static_cast<int>(b.type)) + ":" + std::to_string(static_cast<VkShaderStageFlags>(b.stages)) + ";";
			}
			auto cached = pipeline_layouts.find(key);
			if (cached != pipeline_layouts.end())
			{
				return cached->second;
			}

			SharedPtr<PipelineLayout> layout(new PipelineLayout());
			layout->bindings = bindings;
			std::vector<vk::DescriptorSetLayoutBinding> layout_bindings;
			for (const PipelineLayout::Binding& b : bindings)
			{
				layout_bindings.push_back(vk::DescriptorSetLayoutBinding(b.binding, b.type, 1, b.stages));
			}
			layout->set_layout = vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR, layout_bindings));
			const vk::DescriptorSetLayout set_layout = *layout->set_layout;
			layout->layout = vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo({}, set_layout));
			pipeline_layouts.emplace(key, layout);
			return layout;
		}

		SharedPtr<Pipeline> Device::create_pipeline(const PipelineDesc::Graphics& pipeline_desc, const BindingDesc& binding_desc)
		{
			seAssert(pipeline_desc.vertex_shader != nullptr, "Graphics pipelines need a vertex shader");
			SharedPtr<Pipeline> out_pipeline(new Pipeline(binding_desc, vk::PipelineBindPoint::eGraphics));
			out_pipeline->layout = get_pipeline_layout({ pipeline_desc.vertex_shader.get(), pipeline_desc.geometry_shader.get(), pipeline_desc.pixel_shader.get() });

			std::vector<vk::PipelineShaderStageCreateInfo> stages;
			for (const Shader* shader : std::initializer_list<const Shader*>{ pipeline_desc.vertex_shader.get(), pipeline_desc.geometry_shader.get(), pipeline_desc.pixel_shader.get() })
			{
				if (shader) stages.push_back(vk::PipelineShaderStageCreateInfo({}, shader->get_stage(), shader->get_module(), shader->get_entry_point()));
			}

			// Vertex input: match input layout elements to the vertex shader's inputs by semantic. Strides are dynamic state.
			std::vector<vk::VertexInputBindingDescription> vertex_bindings;
			std::vector<vk::VertexInputAttributeDescription> vertex_attributes;
			const InputLayout::Desc& input_layout = pipeline_desc.input_layout;
			for (u32 i = 0; i < input_layout.num_elements; i++)
			{
				const InputLayout::ElementDesc& element = input_layout.elements[i];
				bool found = false;
				const u32 location = find_vertex_input_location(pipeline_desc.vertex_shader->get_reflection(), element, found);
				if (!found)
				{
					continue; // Element not read by this vertex shader
				}
				vertex_attributes.push_back(vk::VertexInputAttributeDescription(location, element.input_slot, translate(element.format), element.aligned_byte_offset));
				const bool has_binding = std::any_of(vertex_bindings.begin(), vertex_bindings.end(), [&element](const auto& b) { return b.binding == element.input_slot; });
				if (!has_binding)
				{
					const vk::VertexInputRate rate = element.input_classification == InputLayout::InputClassification::PerInstance ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex;
					seAssert(rate == vk::VertexInputRate::eVertex || element.instance_data_step_rate <= 1, "Instance step rates above 1 are not supported");
					vertex_bindings.push_back(vk::VertexInputBindingDescription(element.input_slot, 0, rate));
				}
			}
			for (const SpirvReflection::VertexInput& input : pipeline_desc.vertex_shader->get_reflection().vertex_inputs)
			{
				const bool provided = std::any_of(vertex_attributes.begin(), vertex_attributes.end(), [&input](const auto& a) { return a.location == input.location; });
				seAssert(provided, "Vertex shader input %s is not provided by the input layout", input.semantic.c_str());
				(void)provided;
			}

			const vk::PipelineVertexInputStateCreateInfo vertex_input({}, vertex_bindings, vertex_attributes);
			const vk::PipelineInputAssemblyStateCreateInfo input_assembly({}, translate(pipeline_desc.primitive_topology), VK_FALSE);
			const vk::PipelineViewportStateCreateInfo viewport_state({}, 1, nullptr, 1, nullptr);

			const Rasterizer::Desc& rd = pipeline_desc.rasterizer_desc;
			vk::PipelineRasterizationStateCreateInfo rasterization({}, VK_FALSE, VK_FALSE, translate(rd.fill_mode), translate(rd.cull_mode),
				rd.front_counter_clockwise ? vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f);
			const vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1, VK_FALSE, 0.0f, nullptr, pipeline_desc.blend_desc.alpha_to_coverage_enable);

			const DepthStencil::Desc& ds = pipeline_desc.depth_stencil_desc;
			const vk::PipelineDepthStencilStateCreateInfo depth_stencil({}, ds.depth_enable, ds.depth_enable && ds.depth_write, translate(ds.comparison_function), VK_FALSE,
				ds.stencil_enable, translate_stencil(ds.stencil_front_desc, ds), translate_stencil(ds.stencil_back_desc, ds), 0.0f, 1.0f);

			std::array<vk::PipelineColorBlendAttachmentState, 8> blend_attachments;
			std::array<vk::Format, 8> colour_formats;
			for (u32 i = 0; i < pipeline_desc.render_target_count; i++)
			{
				blend_attachments[i] = translate_blend(pipeline_desc.blend_desc.render_targets[pipeline_desc.blend_desc.independent_blend_enable ? i : 0]);
				colour_formats[i] = translate(pipeline_desc.render_target_format_list[i]);
			}
			const Blend::RenderTargetDesc& rt0 = pipeline_desc.blend_desc.render_targets[0];
			const vk::PipelineColorBlendStateCreateInfo colour_blend({}, rt0.logic_op_enable, translate(rt0.logic_op), pipeline_desc.render_target_count, blend_attachments.data());

			std::vector<vk::DynamicState> dynamic_states = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
			if (!vertex_bindings.empty())
			{
				dynamic_states.push_back(vk::DynamicState::eVertexInputBindingStride);
			}
			const vk::PipelineDynamicStateCreateInfo dynamic_state({}, dynamic_states);

			const DXGI_FORMAT ds_format = pipeline_desc.depth_stencil_format;
			const vk::Format depth_format = ds_format == DXGI_FORMAT_UNKNOWN ? vk::Format::eUndefined : translate(ds_format);
			const vk::PipelineRenderingCreateInfo rendering_info(0, pipeline_desc.render_target_count, colour_formats.data(), depth_format,
				has_stencil(ds_format) ? depth_format : vk::Format::eUndefined);

			vk::GraphicsPipelineCreateInfo create_info({}, stages, &vertex_input, &input_assembly, nullptr, &viewport_state, &rasterization, &multisample,
				&depth_stencil, &colour_blend, &dynamic_state, *out_pipeline->layout->layout);
			create_info.pNext = &rendering_info;
			out_pipeline->pipeline = vk::raii::Pipeline(device, pipeline_cache, create_info);
			return out_pipeline;
		}

		SharedPtr<Pipeline> Device::create_pipeline(const PipelineDesc::Compute& pipeline_desc, const BindingDesc& binding_desc)
		{
			seAssert(pipeline_desc.compute_shader != nullptr, "Compute pipelines need a compute shader");
			SharedPtr<Pipeline> out_pipeline(new Pipeline(binding_desc, vk::PipelineBindPoint::eCompute));
			out_pipeline->layout = get_pipeline_layout({ pipeline_desc.compute_shader.get() });
			const Shader& shader = *pipeline_desc.compute_shader;
			const vk::ComputePipelineCreateInfo create_info({}, vk::PipelineShaderStageCreateInfo({}, shader.get_stage(), shader.get_module(), shader.get_entry_point()), *out_pipeline->layout->layout);
			out_pipeline->pipeline = vk::raii::Pipeline(device, pipeline_cache, create_info);
			return out_pipeline;
		}

		SharedPtr<Pipeline> Device::create_pipeline(const PipelineDesc::Mesh& pipeline_desc, const BindingDesc& binding_desc)
		{
			if (!SupportsMeshShaders())
			{
				return nullptr;
			}
			seAssert(pipeline_desc.mesh_shader != nullptr, "No Mesh Shader Provided");
			seAssert(pipeline_desc.pixel_shader != nullptr, "No Pixel Shader Provided");

			SharedPtr<Pipeline> out_pipeline(new Pipeline(binding_desc, vk::PipelineBindPoint::eGraphics));
			out_pipeline->layout = get_pipeline_layout({ pipeline_desc.amp_shader.get(), pipeline_desc.mesh_shader.get(), pipeline_desc.pixel_shader.get() });

			std::vector<vk::PipelineShaderStageCreateInfo> stages;
			for (const Shader* shader : std::initializer_list<const Shader*>{ pipeline_desc.amp_shader.get(), pipeline_desc.mesh_shader.get(), pipeline_desc.pixel_shader.get() })
			{
				if (shader) stages.push_back(vk::PipelineShaderStageCreateInfo({}, shader->get_stage(), shader->get_module(), shader->get_entry_point()));
			}

			const vk::PipelineViewportStateCreateInfo viewport_state({}, 1, nullptr, 1, nullptr);
			const Rasterizer::Desc& rd = pipeline_desc.rasterizer_desc;
			const vk::PipelineRasterizationStateCreateInfo rasterization({}, VK_FALSE, VK_FALSE, translate(rd.fill_mode), translate(rd.cull_mode),
				rd.front_counter_clockwise ? vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f);
			const vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1, VK_FALSE, 0.0f, nullptr, pipeline_desc.blend_desc.alpha_to_coverage_enable);
			const DepthStencil::Desc& ds = pipeline_desc.depth_stencil_desc;
			const vk::PipelineDepthStencilStateCreateInfo depth_stencil({}, ds.depth_enable, ds.depth_enable && ds.depth_write, translate(ds.comparison_function), VK_FALSE,
				ds.stencil_enable, translate_stencil(ds.stencil_front_desc, ds), translate_stencil(ds.stencil_back_desc, ds), 0.0f, 1.0f);

			std::array<vk::PipelineColorBlendAttachmentState, 8> blend_attachments;
			std::array<vk::Format, 8> colour_formats;
			for (u32 i = 0; i < pipeline_desc.render_target_count; i++)
			{
				blend_attachments[i] = translate_blend(pipeline_desc.blend_desc.render_targets[pipeline_desc.blend_desc.independent_blend_enable ? i : 0]);
				colour_formats[i] = translate(pipeline_desc.render_target_format_list[i]);
			}
			const Blend::RenderTargetDesc& rt0 = pipeline_desc.blend_desc.render_targets[0];
			const vk::PipelineColorBlendStateCreateInfo colour_blend({}, rt0.logic_op_enable, translate(rt0.logic_op), pipeline_desc.render_target_count, blend_attachments.data());
			const std::array<vk::DynamicState, 2> dynamic_states = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
			const vk::PipelineDynamicStateCreateInfo dynamic_state({}, dynamic_states);

			const DXGI_FORMAT ds_format = pipeline_desc.depth_stencil_format;
			const vk::Format depth_format = ds_format == DXGI_FORMAT_UNKNOWN ? vk::Format::eUndefined : translate(ds_format);
			const vk::PipelineRenderingCreateInfo rendering_info(0, pipeline_desc.render_target_count, colour_formats.data(), depth_format,
				has_stencil(ds_format) ? depth_format : vk::Format::eUndefined);

			// Mesh pipelines have no vertex input or input assembly state
			vk::GraphicsPipelineCreateInfo create_info({}, stages, nullptr, nullptr, nullptr, &viewport_state, &rasterization, &multisample,
				&depth_stencil, &colour_blend, &dynamic_state, *out_pipeline->layout->layout);
			create_info.pNext = &rendering_info;
			out_pipeline->pipeline = vk::raii::Pipeline(device, pipeline_cache, create_info);
			return out_pipeline;
		}

		SharedPtr<Buffer> Device::create_buffer(SharedPtr<Memory> memory, u32 size, BufferType type, bool uav_access)
		{
			// Buffers get every usage the engine might need (usage flags do not affect buffer performance on desktop GPUs)
			const vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
				vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
				vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndirectBuffer;
			vk::raii::Buffer vk_buffer(device, vk::BufferCreateInfo({}, size, usage, vk::SharingMode::eExclusive));

			const vk::MemoryRequirements requirements = vk_buffer.getMemoryRequirements();
			seAssert(requirements.size <= memory->get_size(), "Buffer does not fit in its memory");
			seAssert((requirements.memoryTypeBits & (1u << memory->get_memory_type_index())) != 0, "Memory type not compatible with buffer");
			CHECKVK(vmaBindBufferMemory(allocator, memory->get_allocation(), static_cast<VkBuffer>(*vk_buffer)));

			const bool upload = memory->get_type() == MemoryType::Upload;
			const bool readback = memory->get_type() == MemoryType::Readback;
			SharedPtr<Buffer> buffer(new Buffer(type, size, uav_access, upload, readback));
			buffer->memory = memory;
			buffer->buffer = std::move(vk_buffer);
			return buffer;
		}

		SharedPtr<Texture> Device::create_texture(SharedPtr<Memory> memory, u32 size, const ResourceCreateDesc& resource_desc)
		{
			vk::ImageCreateInfo image_info;
			image_info.imageType = resource_desc.dimension == ResourceDimension::Texture1D ? vk::ImageType::e1D : (resource_desc.dimension == ResourceDimension::Texture3D ? vk::ImageType::e3D : vk::ImageType::e2D);
			image_info.format = translate(resource_desc.format);
			image_info.extent = vk::Extent3D(resource_desc.width, resource_desc.height, resource_desc.dimension == ResourceDimension::Texture3D ? resource_desc.depth : 1);
			image_info.mipLevels = std::max(1u, resource_desc.mip_count);
			image_info.arrayLayers = resource_desc.dimension == ResourceDimension::Texture3D ? 1 : std::max(1u, resource_desc.depth);
			image_info.samples = vk::SampleCountFlagBits::e1;
			image_info.tiling = vk::ImageTiling::eOptimal;
			image_info.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
			if ((resource_desc.usage_flags & ResourceUsageFlags::RenderTarget) == ResourceUsageFlags::RenderTarget) image_info.usage |= vk::ImageUsageFlagBits::eColorAttachment;
			if ((resource_desc.usage_flags & ResourceUsageFlags::DepthStencil) == ResourceUsageFlags::DepthStencil) image_info.usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
			if ((resource_desc.usage_flags & ResourceUsageFlags::UnorderedAccess) == ResourceUsageFlags::UnorderedAccess) image_info.usage |= vk::ImageUsageFlagBits::eStorage;
			image_info.initialLayout = vk::ImageLayout::eUndefined;

			vk::raii::Image image(device, image_info);
			const vk::MemoryRequirements requirements = image.getMemoryRequirements();
			seAssert(requirements.size <= memory->get_size(), "Texture does not fit in its memory");
			seAssert((requirements.memoryTypeBits & (1u << memory->get_memory_type_index())) != 0, "Memory type not compatible with texture");
			CHECKVK(vmaBindImageMemory(allocator, memory->get_allocation(), static_cast<VkImage>(*image)));

			SharedPtr<Texture> texture(new Texture(*this, resource_desc));
			texture->memory = memory;
			texture->size_bytes = size;
			texture->image = std::move(image);
			texture->image_handle = *texture->image;
			// Contents are undefined until first written, like D3D12 resources in heaps created with CREATE_NOT_ZEROED
			add_pending_initial_transition(texture->image_handle, texture->get_aspect(), texture->get_read_resource_state());
			return texture;
		}

		SharedPtr<Texture> Device::create_texture_from_dds(const void* dds_memory, u64 dds_size, sg::UploadHeap& upload_heap)
		{
			DDSTextureData dds;
			if (!parse_dds(dds_memory, dds_size, dds))
			{
				seWriteLine("Failed to load DDS texture");
				return nullptr;
			}
			const SizeAndAlignment size_align = calculate_resource_size_alignment(dds.desc);
			SharedPtr<Memory> memory = allocate_memory(MemoryType::GPUOptimal, MemorySubType::Texture, size_align.size, size_align.alignment);
			SharedPtr<Texture> texture = create_texture(memory, static_cast<u32>(size_align.size), dds.desc);
			for (u32 i = 0; i < dds.subresources.size(); i++)
			{
				const DDSTextureData::Subresource& sub = dds.subresources[i];
				upload_heap.upload_texture_subresource(texture.get(), i, sub.data, sub.row_pitch, sub.row_count);
			}
			return texture;
		}

		SharedPtr<IndirectArguments> Device::create_indirect_arguments()
		{
			// Currently ONLY used for mesh shader dispatch (VkDrawMeshTasksIndirectCommandEXT)
			SharedPtr<IndirectArguments> indirect_arguments(new IndirectArguments());
			SharedPtr<Memory> indirect_memory = allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024);
			indirect_arguments->indirect_buffer = create_buffer(indirect_memory, 64u * 1024u, BufferType::GeneralDataBuffer, true);
			return indirect_arguments;
		}

		SharedPtr<RenderTargetView> Device::create_render_target_view(SharedPtr<Texture>& texture, DXGI_FORMAT format)
		{
			seAssert(texture.get(), "Invalid texture pointer");
			seAssert(format == DXGI_FORMAT_UNKNOWN || format == texture->resource_create_desc.format, "Render target views must use the texture format in Vulkan (no MUTABLE_FORMAT)");
			SharedPtr<RenderTargetView> rtv(new RenderTargetView());
			rtv->texture_resource = texture;
			rtv->format = format;
			return rtv;
		}

		SharedPtr<DepthStencilView> Device::create_depth_stencil_view(SharedPtr<Texture>& texture, DXGI_FORMAT format)
		{
			seAssert(texture.get(), "Invalid texture pointer");
			seAssert(format == DXGI_FORMAT_UNKNOWN || format == texture->resource_create_desc.format, "Depth stencil views must use the texture format in Vulkan (no MUTABLE_FORMAT)");
			SharedPtr<DepthStencilView> dsv(new DepthStencilView());
			dsv->texture_resource = texture;
			dsv->format = format;
			return dsv;
		}

		ConstantBufferView Device::create_constant_buffer_view(Buffer* buffer, u64 offset, u64 size)
		{
			seAssert(buffer != nullptr, "Invalid buffer");
			seAssert(buffer->get_type() == BufferType::Constant, "expecting constant buffer");
			seAssert(offset % properties.limits.minUniformBufferOffsetAlignment == 0, "Invalid offset");
			seAssert(size > 0 && size <= properties.limits.maxUniformBufferRange, "Invalid size");
			ConstantBufferView cbv;
			cbv.buffer = buffer->get();
			cbv.offset = offset;
			cbv.size = size;
			return cbv;
		}

		ShaderResourceView Device::create_shader_resource_view(SharedPtr<Buffer> buffer, u64 element_size, u64 element_count)
		{
			ShaderResourceView srv;
			srv.buffer_resource = buffer;
			srv.element_size = element_size;
			srv.element_count = element_count;
			return srv;
		}

		ShaderResourceView Device::create_shader_resource_view(SharedPtr<Texture> texture, u32 top_mip, u32 mip_count)
		{
			ShaderResourceView srv;
			srv.texture_resource = texture;
			srv.top_mip = top_mip;
			srv.mip_count = mip_count;
			return srv;
		}

		UnorderedAccessView Device::create_unordered_access_view(SharedPtr<Buffer> buffer, u64 element_size, u64 element_count)
		{
			UnorderedAccessView uav;
			uav.buffer_resource = buffer;
			uav.element_size = element_size;
			uav.element_count = element_count;
			return uav;
		}

		UnorderedAccessView Device::create_unordered_access_view(SharedPtr<Texture> texture, u32 mip_level)
		{
			seAssert(texture->resource_create_desc.dimension == ResourceDimension::Texture2D, "Only supported this path currently");
			UnorderedAccessView uav;
			uav.texture_resource = texture;
			uav.mip_level = mip_level;
			return uav;
		}

		VertexBufferView Device::create_vertex_buffer_view(SharedPtr<Buffer> buffer, u64 offset, u64 size, u64 stride)
		{
			VertexBufferView vbv;
			vbv.buffer_resource = buffer;
			vbv.offset = offset;
			vbv.size = size;
			vbv.stride = stride;
			return vbv;
		}

		IndexBufferView Device::create_index_buffer_view(SharedPtr<Buffer> buffer, u64 offset, u64 size, DXGI_FORMAT format)
		{
			IndexBufferView ibv;
			ibv.buffer_resource = buffer;
			ibv.offset = offset;
			ibv.size = size;
			ibv.index_type = translate_index_type(format);
			return ibv;
		}

		u32 Device::GetFormatBitsPerUnit(DXGI_FORMAT format)
		{
			const FormatInfo info = get_format_info(format);
			return (info.block_bytes * 8) / (info.block_width * info.block_height);
		}

		u32 Device::GetFormatWidthAlignment(DXGI_FORMAT format)
		{
			return get_format_info(format).block_width;
		}

		u64 Device::CalculateResourceSize(u32 width, u32 height, u32 depth, u32 mip_levels, DXGI_FORMAT format)
		{
			const FormatInfo info = get_format_info(format);
			u64 total = 0;
			for (u32 mip = 0; mip < std::max(1u, mip_levels); mip++)
			{
				const u32 w = std::max(1u, width >> mip);
				const u32 h = std::max(1u, height >> mip);
				const u32 d = std::max(1u, depth >> mip);
				total += static_cast<u64>((w + info.block_width - 1) / info.block_width) * info.block_bytes * ((h + info.block_height - 1) / info.block_height) * d;
			}
			return total;
		}

		vk::raii::Sampler Device::create_sampler(const sg::SamplerDesc& desc)
		{
			const vk::Filter L = vk::Filter::eLinear;
			const vk::Filter N = vk::Filter::eNearest;
			const vk::SamplerMipmapMode ML = vk::SamplerMipmapMode::eLinear;
			const vk::SamplerMipmapMode MN = vk::SamplerMipmapMode::eNearest;
			vk::Filter min_filter = L, mag_filter = L;
			vk::SamplerMipmapMode mip_mode = ML;
			bool anisotropic = false;
			switch (desc.filter)
			{
			case Filter::MinMagMipPoint: min_filter = N; mag_filter = N; mip_mode = MN; break;
			case Filter::MinMagPointMipLinear: min_filter = N; mag_filter = N; mip_mode = ML; break;
			case Filter::MinPointMagLinearMipPoint: min_filter = N; mag_filter = L; mip_mode = MN; break;
			case Filter::MinPointMagMipLinear: min_filter = N; mag_filter = L; mip_mode = ML; break;
			case Filter::MinLinearMagMipPoint: min_filter = L; mag_filter = N; mip_mode = MN; break;
			case Filter::MinLinearMagPointMipLinear: min_filter = L; mag_filter = N; mip_mode = ML; break;
			case Filter::MinMagLinearMipPoint: min_filter = L; mag_filter = L; mip_mode = MN; break;
			case Filter::MinMagMipLinear: break;
			case Filter::MinMagAnisotropicMipPoint: mip_mode = MN; anisotropic = true; break;
			case Filter::Anisotropic: anisotropic = true; break;
			}
			const float max_anisotropy = std::clamp(static_cast<float>(desc.max_anisotropy), 1.0f, properties.limits.maxSamplerAnisotropy);
			const float max_bias = properties.limits.maxSamplerLodBias;
			const bool compare = desc.comparison_func != ComparisonFunction::None;
			const vk::SamplerCreateInfo info({}, mag_filter, min_filter, mip_mode, translate(desc.address_u), translate(desc.address_v), translate(desc.address_w),
				std::clamp(desc.mip_lod_bias, -max_bias, max_bias), anisotropic ? VK_TRUE : VK_FALSE, max_anisotropy, compare ? VK_TRUE : VK_FALSE,
				translate(desc.comparison_func), desc.min_lod, desc.max_lod, translate_border_colour(desc.border_color));
			return vk::raii::Sampler(device, info);
		}

		void Device::AddSamplers(const char* sampler_name, const sg::SamplerDesc& sampler_desc)
		{
			SamplerInfo info;
			info.name = sampler_name;
			info.sampler_desc = sampler_desc;
			info.sampler = create_sampler(sampler_desc);
			samplers.push_back(std::move(info));
		}

		void Device::RecreateSamplers()
		{
			// Called between frames (the previous frame has completed), and samplers are pushed with each draw's descriptors
			for (SamplerInfo& info : samplers)
			{
				info.sampler = create_sampler(info.sampler_desc);
			}
		}

		void Device::add_pending_initial_transition(vk::Image image, vk::ImageAspectFlags aspect, ResourceState state)
		{
			const StateInfo after = get_state_info(state, shader_stages);
			const vk::ImageMemoryBarrier2 barrier(vk::PipelineStageFlagBits2::eNone, vk::AccessFlagBits2::eNone, after.stages, after.access,
				vk::ImageLayout::eUndefined, after.layout, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image,
				vk::ImageSubresourceRange(aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS));
			std::lock_guard<std::mutex> lock(pending_transitions_mutex);
			pending_initial_transitions.push_back(barrier);
		}

		void Device::remove_pending_initial_transition(vk::Image image)
		{
			std::lock_guard<std::mutex> lock(pending_transitions_mutex);
			std::erase_if(pending_initial_transitions, [image](const vk::ImageMemoryBarrier2& b) { return b.image == image; });
		}

		void Device::take_pending_initial_transitions(std::vector<vk::ImageMemoryBarrier2>& out_barriers)
		{
			std::lock_guard<std::mutex> lock(pending_transitions_mutex);
			out_barriers.swap(pending_initial_transitions);
			pending_initial_transitions.clear();
		}

		// ---------------------------------------------------------------------------------------------------------------------
		// Swap chain
		//
		// Synchronisation: after acquiring an image, an empty submission waits on the acquire semaphore at ALL_COMMANDS, which
		// orders all later work on the queue after the image is available (matching D3D12, where back buffers are always usable).
		// Before presenting, an empty submission signals that image's present semaphore after all earlier work.
		// ---------------------------------------------------------------------------------------------------------------------

		u32 Device::create_swap_chain(HWND hwnd, CommandQueue* command_queue, u32 buffer_count, DXGI_FORMAT format, u32 width, u32 height, SharedPtr<RenderTargetView>* rtv_list)
		{
			swap_chain_hwnd = hwnd;
			present_queue = command_queue;
			swap_chain_buffer_count = buffer_count;

			surface = vk::raii::SurfaceKHR(instance, vk::Win32SurfaceCreateInfoKHR({}, GetModuleHandle(nullptr), hwnd));
			seAssert(physical_device.getSurfaceSupportKHR(queue_family, *surface), "Queue family cannot present to this surface");

			// Prefer the requested format, then the formats every Windows driver supports
			const std::vector<vk::SurfaceFormatKHR> formats = physical_device.getSurfaceFormatsKHR(*surface);
			const vk::Format requested = translate(format);
			swap_chain_format = formats[0];
			for (vk::Format candidate : { requested, vk::Format::eB8G8R8A8Unorm, vk::Format::eR8G8B8A8Unorm })
			{
				auto it = std::find_if(formats.begin(), formats.end(), [candidate](const vk::SurfaceFormatKHR& f) { return f.format == candidate && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });
				if (it != formats.end())
				{
					swap_chain_format = *it;
					break;
				}
			}
			swap_chain_extent = vk::Extent2D(width, height);

			create_swap_chain_resources(VK_NULL_HANDLE);

			acquire_semaphores.clear();
			present_semaphores.clear();
			for (u32 i = 0; i < swap_chain_buffer_count + 1; i++) acquire_semaphores.emplace_back(device, vk::SemaphoreCreateInfo());
			for (u32 i = 0; i < swap_chain_buffer_count; i++) present_semaphores.emplace_back(device, vk::SemaphoreCreateInfo());

			for (u32 i = 0; i < swap_chain_buffer_count; i++)
			{
				rtv_list[i] = create_render_target_view(swap_chain_textures[i]);
			}

			if (!acquire_next_image())
			{
				enter_minimised_mode();
			}
			return swap_chain_current_index;
		}

		void Device::create_swap_chain_resources(vk::SwapchainKHR old_swap_chain)
		{
			const vk::SurfaceCapabilitiesKHR caps = physical_device.getSurfaceCapabilitiesKHR(*surface);
			vk::Extent2D extent = swap_chain_extent;
			if (caps.currentExtent.width != 0xFFFFFFFFu)
			{
				extent = caps.currentExtent;
			}
			extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
			extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
			seAssert(extent == swap_chain_extent || swap_chain_textures.empty(), "Swap chain size changed (window resizing is not supported)");

			u32 image_count = std::max(swap_chain_buffer_count, caps.minImageCount);
			if (caps.maxImageCount != 0) image_count = std::min(image_count, caps.maxImageCount);

			const vk::ImageUsageFlags usage = (vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc) & caps.supportedUsageFlags;
			const vk::SwapchainCreateInfoKHR create_info({}, *surface, image_count, swap_chain_format.format, swap_chain_format.colorSpace, extent, 1, usage,
				vk::SharingMode::eExclusive, {}, caps.currentTransform, vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::PresentModeKHR::eFifo, VK_TRUE, old_swap_chain);
			swap_chain = vk::raii::SwapchainKHR(device, create_info);

			const std::vector<vk::Image> images = swap_chain.getImages();
			seAssert(images.size() == swap_chain_buffer_count, "Swap chain created %u images, %u requested", static_cast<u32>(images.size()), swap_chain_buffer_count);

			if (swap_chain_textures.empty())
			{
				ResourceCreateDesc rcd(extent.width, extent.height, translate(swap_chain_format.format), ResourceUsageFlags::RenderTarget);
				for (u32 i = 0; i < swap_chain_buffer_count; i++)
				{
					swap_chain_textures.push_back(SharedPtr<Texture>(new Texture(*this, rcd, true)));
				}
			}
			swap_chain_image_initialised.assign(swap_chain_buffer_count, false);
			for (u32 i = 0; i < swap_chain_buffer_count; i++)
			{
				swap_chain_textures[i]->set_external_image(images[std::min<size_t>(i, images.size() - 1)], true);
			}
		}

		bool Device::surface_has_area()
		{
			const vk::SurfaceCapabilitiesKHR caps = physical_device.getSurfaceCapabilitiesKHR(*surface);
			return caps.currentExtent.width != 0 && caps.currentExtent.height != 0;
		}

		bool Device::acquire_next_image()
		{
			const vk::Semaphore acquire_semaphore = *acquire_semaphores[acquire_semaphore_index];
			u32 image_index = 0;
			VkResult result = device.getDispatcher()->vkAcquireNextImageKHR(static_cast<VkDevice>(*device), static_cast<VkSwapchainKHR>(*swap_chain), UINT64_MAX,
				static_cast<VkSemaphore>(acquire_semaphore), VK_NULL_HANDLE, &image_index);
			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				if (!surface_has_area())
				{
					return false;
				}
				recreate_swap_chain();
				result = device.getDispatcher()->vkAcquireNextImageKHR(static_cast<VkDevice>(*device), static_cast<VkSwapchainKHR>(*swap_chain), UINT64_MAX,
					static_cast<VkSemaphore>(acquire_semaphore), VK_NULL_HANDLE, &image_index);
			}
			if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			{
				return false;
			}
			acquire_semaphore_index = (acquire_semaphore_index + 1) % static_cast<u32>(acquire_semaphores.size());

			present_queue->submit_semaphores(acquire_semaphore, VK_NULL_HANDLE);
			if (!swap_chain_image_initialised[image_index])
			{
				add_pending_initial_transition(swap_chain_textures[image_index]->get(), vk::ImageAspectFlagBits::eColor, ResourceState::Present);
				swap_chain_image_initialised[image_index] = true;
			}
			swap_chain_current_index = image_index;
			return true;
		}

		void Device::recreate_swap_chain()
		{
			device.waitIdle();
			vk::raii::SwapchainKHR old_swap_chain = std::move(swap_chain);
			create_swap_chain_resources(*old_swap_chain);
		}

		void Device::enter_minimised_mode()
		{
			// Keep rendering into an offscreen image so the application loop is unchanged; nothing is presented
			device.waitIdle();
			if (!minimised_back_buffer)
			{
				const ResourceCreateDesc& swap_desc = swap_chain_textures[0]->resource_create_desc;
				ResourceCreateDesc rcd(swap_desc.width, swap_desc.height, swap_desc.format, ResourceUsageFlags::RenderTarget);
				const SizeAndAlignment size_align = calculate_resource_size_alignment(rcd);
				SharedPtr<Memory> memory = allocate_memory(MemoryType::GPUOptimal, MemorySubType::Target, size_align.size, size_align.alignment);
				minimised_back_buffer = create_texture(memory, static_cast<u32>(size_align.size), rcd);
			}
			for (SharedPtr<Texture>& texture : swap_chain_textures)
			{
				texture->set_external_image(minimised_back_buffer->get(), false);
			}
			swap_chain_minimised = true;
		}

		u32 Device::present_swap_chain(CommandQueue* command_queue)
		{
			if (swap_chain_minimised)
			{
				if (!surface_has_area())
				{
					return swap_chain_current_index;
				}
				// Restored: rebuild the swap chain and point the textures back at its images
				recreate_swap_chain();
				swap_chain_minimised = false;
				if (!acquire_next_image())
				{
					enter_minimised_mode();
				}
				return swap_chain_current_index;
			}

			const vk::Semaphore present_semaphore = *present_semaphores[swap_chain_current_index];
			command_queue->submit_semaphores(VK_NULL_HANDLE, present_semaphore);

			const VkSemaphore wait_semaphore = static_cast<VkSemaphore>(present_semaphore);
			const VkSwapchainKHR vk_swap_chain = static_cast<VkSwapchainKHR>(*swap_chain);
			VkPresentInfoKHR present_info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
			present_info.waitSemaphoreCount = 1;
			present_info.pWaitSemaphores = &wait_semaphore;
			present_info.swapchainCount = 1;
			present_info.pSwapchains = &vk_swap_chain;
			present_info.pImageIndices = &swap_chain_current_index;
			const VkResult result = device.getDispatcher()->vkQueuePresentKHR(static_cast<VkQueue>(command_queue->get()), &present_info);
			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			{
				if (!surface_has_area())
				{
					enter_minimised_mode();
					return swap_chain_current_index;
				}
				recreate_swap_chain();
			}
			else
			{
				seAssert(result == VK_SUCCESS, "vkQueuePresentKHR failed (%i)", static_cast<int>(result));
			}

			if (!acquire_next_image())
			{
				enter_minimised_mode();
			}
			return swap_chain_current_index;
		}
	}
}
