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
			constexpr u32 kApiVersion = VK_API_VERSION_1_4;

			// The device extensions the renderer is built on. Swap chain is needed to present, mesh shader for mesh/amplification
			// shader rendering (enabled when supported).
			const char* const kRequiredDeviceExtensions[] = {
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
				VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
				VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
				VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
			};

			constexpr u32 kMaxSamplers = BindingShift::UAV - BindingShift::SAMPLER;
			constexpr u32 kResourceHeapRegions = 8;

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

			// Memory allocator. All memory supports device addresses: buffers are referenced by address in descriptors and CBVs.
			{
				VmaAllocatorCreateInfo create_info = {};
				create_info.vulkanApiVersion = kApiVersion;
				create_info.instance = static_cast<VkInstance>(*instance);
				create_info.physicalDevice = static_cast<VkPhysicalDevice>(*physical_device);
				create_info.device = static_cast<VkDevice>(*device);
				create_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT | VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;
				CHECKVK(vmaCreateAllocator(&create_info, &allocator));
			}

			// Memory types every buffer / texture / target can be bound to. Resources are bound into memory allocated before
			// they are created (as with D3D12 placed resources), so the allocation must use a type compatible with all of them.
			{
				const vk::BufferUsageFlags buffer_usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
					vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
					vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
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

			create_descriptor_heaps();
			create_descriptor_mapping();
			create_null_descriptors();

			// Sampler used to display textures through ImGui (whose backend uses classic descriptor sets)
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
			null_buffer = nullptr;
			null_texture = nullptr;
			resource_heap.reset();
			sampler_heap.reset();
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

			// Prefer a discrete GPU supporting Vulkan 1.4 and the required extensions, with a graphics queue that can present
			int best_score = -1;
			for (vk::raii::PhysicalDevice& candidate : physical_devices)
			{
				const vk::PhysicalDeviceProperties candidate_properties = candidate.getProperties();
				seWriteLine("Found Vulkan physical device: %s", candidate_properties.deviceName.data());
				if (candidate_properties.apiVersion < kApiVersion)
				{
					continue;
				}
				const std::vector<vk::ExtensionProperties> available = candidate.enumerateDeviceExtensionProperties();
				const bool has_all = std::all_of(std::begin(kRequiredDeviceExtensions), std::end(kRequiredDeviceExtensions), [&available](const char* name) { return has_extension(available, name); });
				if (!has_all)
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

			seAssert(best_score >= 0, "No Vulkan 1.4 device with descriptor heap, shader object and a graphics + present queue found");
			if (best_score < 0)
			{
				MessageBoxA(nullptr, "No GPU supporting Vulkan 1.4 with VK_EXT_descriptor_heap and VK_EXT_shader_object was found.", "Slim Graphics", MB_OK | MB_ICONERROR);
				ExitProcess(1);
			}
			properties = physical_device.getProperties();
		}

		void Device::create_logical_device()
		{
			const std::vector<vk::ExtensionProperties> available = physical_device.enumerateDeviceExtensionProperties();

			// Promoted features are enabled through their own structures: VkPhysicalDeviceVulkan12Features may not be combined with
			// VK_EXT_descriptor_indexing (VUID-VkDeviceCreateInfo-pNext-04748).
			using Features = vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features,
				vk::PhysicalDeviceTimelineSemaphoreFeatures, vk::PhysicalDeviceHostQueryResetFeatures, vk::PhysicalDeviceBufferDeviceAddressFeatures,
				vk::PhysicalDeviceDescriptorIndexingFeatures, vk::PhysicalDeviceScalarBlockLayoutFeatures, vk::PhysicalDeviceMaintenance5Features, vk::PhysicalDeviceShaderObjectFeaturesEXT,
				vk::PhysicalDeviceDescriptorHeapFeaturesEXT, vk::PhysicalDeviceMeshShaderFeaturesEXT>;
			const Features supported = physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features,
				vk::PhysicalDeviceTimelineSemaphoreFeatures, vk::PhysicalDeviceHostQueryResetFeatures, vk::PhysicalDeviceBufferDeviceAddressFeatures,
				vk::PhysicalDeviceDescriptorIndexingFeatures, vk::PhysicalDeviceScalarBlockLayoutFeatures, vk::PhysicalDeviceMaintenance5Features, vk::PhysicalDeviceShaderObjectFeaturesEXT,
				vk::PhysicalDeviceDescriptorHeapFeaturesEXT, vk::PhysicalDeviceMeshShaderFeaturesEXT>();
			const vk::PhysicalDeviceFeatures& core = supported.get<vk::PhysicalDeviceFeatures2>().features;
			const vk::PhysicalDeviceVulkan13Features& core13 = supported.get<vk::PhysicalDeviceVulkan13Features>();
			const vk::PhysicalDeviceMeshShaderFeaturesEXT& mesh = supported.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>();
			seAssert(core13.dynamicRendering && core13.synchronization2 && supported.get<vk::PhysicalDeviceTimelineSemaphoreFeatures>().timelineSemaphore &&
				supported.get<vk::PhysicalDeviceHostQueryResetFeatures>().hostQueryReset && supported.get<vk::PhysicalDeviceBufferDeviceAddressFeatures>().bufferDeviceAddress &&
				supported.get<vk::PhysicalDeviceShaderObjectFeaturesEXT>().shaderObject && supported.get<vk::PhysicalDeviceDescriptorHeapFeaturesEXT>().descriptorHeap &&
				supported.get<vk::PhysicalDeviceScalarBlockLayoutFeatures>().scalarBlockLayout,
				"Required Vulkan features missing");

			std::vector<const char*> extensions(std::begin(kRequiredDeviceExtensions), std::end(kRequiredDeviceExtensions));
			mesh_shaders_supported = has_extension(available, VK_EXT_MESH_SHADER_EXTENSION_NAME) && mesh.meshShader && mesh.taskShader;
			if (mesh_shaders_supported)
			{
				extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
			}

			// Enable only what the renderer uses
			Features enabled;
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
			geometry_shaders_supported = f.geometryShader;
			logic_op_supported = f.logicOp;
			enabled.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = supported.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters;
			vk::PhysicalDeviceVulkan13Features& e13 = enabled.get<vk::PhysicalDeviceVulkan13Features>();
			e13.dynamicRendering = VK_TRUE;
			e13.synchronization2 = VK_TRUE;
			e13.maintenance4 = core13.maintenance4;
			e13.subgroupSizeControl = core13.subgroupSizeControl;
			e13.shaderDemoteToHelperInvocation = core13.shaderDemoteToHelperInvocation;
			enabled.get<vk::PhysicalDeviceTimelineSemaphoreFeatures>().timelineSemaphore = VK_TRUE;
			enabled.get<vk::PhysicalDeviceHostQueryResetFeatures>().hostQueryReset = VK_TRUE;
			enabled.get<vk::PhysicalDeviceBufferDeviceAddressFeatures>().bufferDeviceAddress = VK_TRUE;
			// Shaders are compiled with D3D buffer layouts (-fvk-use-dx-layout), e.g. tightly packed float3s in structured buffers
			enabled.get<vk::PhysicalDeviceScalarBlockLayoutFeatures>().scalarBlockLayout = VK_TRUE;
			enabled.get<vk::PhysicalDeviceMaintenance5Features>().maintenance5 = VK_TRUE;
			enabled.get<vk::PhysicalDeviceShaderObjectFeaturesEXT>().shaderObject = VK_TRUE;
			enabled.get<vk::PhysicalDeviceDescriptorHeapFeaturesEXT>().descriptorHeap = VK_TRUE;
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

			const float queue_priority = 1.0f;
			const vk::DeviceQueueCreateInfo queue_create_info({}, queue_family, 1, &queue_priority);
			vk::DeviceCreateInfo device_create_info({}, queue_create_info, {}, extensions);
			device_create_info.pNext = &enabled.get<vk::PhysicalDeviceFeatures2>();
			device = vk::raii::Device(physical_device, device_create_info);

			// Unlike D3D12, queues are created with the device
			queue = vk::raii::Queue(device, queue_family, 0);

			// Capabilities used elsewhere
			auto props = physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDescriptorHeapPropertiesEXT, vk::PhysicalDeviceSubgroupProperties, vk::PhysicalDeviceSubgroupSizeControlProperties>();
			heap_properties = props.get<vk::PhysicalDeviceDescriptorHeapPropertiesEXT>();
			heap_properties.pNext = nullptr;
			const vk::PhysicalDeviceSubgroupProperties& subgroup = props.get<vk::PhysicalDeviceSubgroupProperties>();
			const vk::PhysicalDeviceSubgroupSizeControlProperties& subgroup_size = props.get<vk::PhysicalDeviceSubgroupSizeControlProperties>();
			const vk::SubgroupFeatureFlags wave_ops = vk::SubgroupFeatureFlagBits::eBasic | vk::SubgroupFeatureFlagBits::eVote | vk::SubgroupFeatureFlagBits::eArithmetic | vk::SubgroupFeatureFlagBits::eBallot;
			wave_ops_supported = (subgroup.supportedOperations & wave_ops) == wave_ops && (subgroup.supportedStages & vk::ShaderStageFlagBits::eFragment) && (subgroup.supportedStages & vk::ShaderStageFlagBits::eCompute);
			wave_lane_count_min = core13.subgroupSizeControl ? subgroup_size.minSubgroupSize : subgroup.subgroupSize;
			wave_lane_count_max = core13.subgroupSizeControl ? subgroup_size.maxSubgroupSize : subgroup.subgroupSize;

			using S = vk::PipelineStageFlagBits2;
			shader_stages = S::eVertexShader | S::eFragmentShader | S::eComputeShader;
			if (geometry_shaders_supported) shader_stages |= S::eGeometryShader;
			if (mesh_shaders_supported) shader_stages |= S::eTaskShaderEXT | S::eMeshShaderEXT;
		}

		void Device::create_descriptor_heaps()
		{
			// One slot size for every resource descriptor type so tables are simple arrays (like a D3D12 CBV_SRV_UAV heap)
			const u64 slot_alignment = std::max(heap_properties.imageDescriptorAlignment, heap_properties.bufferDescriptorAlignment);
			descriptor_slot_size = static_cast<u32>(AlignUp(std::max(heap_properties.imageDescriptorSize, heap_properties.bufferDescriptorSize), slot_alignment));

			const u64 region_bytes = static_cast<u64>(COMMAND_LIST_DESCRIPTOR_SLOTS) * descriptor_slot_size;
			const u64 reserved = heap_properties.minResourceHeapReservedRange;
			const u32 regions = static_cast<u32>(std::min<u64>(kResourceHeapRegions, (heap_properties.maxResourceHeapSize - reserved - slot_alignment) / region_bytes));
			seAssert(regions > 0, "Resource heap too small for a command list descriptor region");

			DescriptorHeap::CreateDesc resource_desc;
			resource_desc.usable_size = regions * region_bytes;
			resource_desc.reserved_size = reserved;
			resource_desc.reserved_alignment = slot_alignment;
			resource_desc.heap_alignment = heap_properties.resourceHeapAlignment;
			resource_desc.name = "Resource Descriptor Heap";
			resource_heap = std::make_unique<DescriptorHeap>(device, allocator, resource_desc);
			for (u32 i = regions; i > 0; i--)
			{
				free_descriptor_regions.push_back((i - 1) * COMMAND_LIST_DESCRIPTOR_SLOTS);
			}

			DescriptorHeap::CreateDesc sampler_desc;
			sampler_desc.usable_size = static_cast<u64>(kMaxSamplers) * heap_properties.samplerDescriptorSize;
			sampler_desc.reserved_size = heap_properties.minSamplerHeapReservedRange;
			sampler_desc.reserved_alignment = heap_properties.samplerDescriptorAlignment;
			sampler_desc.heap_alignment = heap_properties.samplerHeapAlignment;
			sampler_desc.name = "Sampler Descriptor Heap";
			sampler_heap = std::make_unique<DescriptorHeap>(device, allocator, sampler_desc);
		}

		void Device::create_descriptor_mapping()
		{
			// One mapping from DXC's register based set/binding numbers to descriptor heap / push data sources, shared by every
			// shader object (see PushData in sgVulkanPipeline.h).
			const VkSpirvResourceTypeFlagsEXT srv_types = VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT | VK_SPIRV_RESOURCE_TYPE_READ_ONLY_IMAGE_BIT_EXT |
				VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT | VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT;
			const VkSpirvResourceTypeFlagsEXT uav_types = VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT | VK_SPIRV_RESOURCE_TYPE_READ_ONLY_IMAGE_BIT_EXT |
				VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT | VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT;

			auto add_mapping = [this](u32 first_binding, u32 binding_count, VkSpirvResourceTypeFlagsEXT types, VkDescriptorMappingSourceEXT source, const VkDescriptorMappingSourceDataEXT& data)
				{
					VkDescriptorSetAndBindingMappingEXT mapping = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT };
					mapping.descriptorSet = 0;
					mapping.firstBinding = first_binding;
					mapping.bindingCount = binding_count;
					mapping.resourceMask = types;
					mapping.source = source;
					mapping.sourceData = data;
					descriptor_mappings.push_back(mapping);
				};

			// b0-b3: constant buffers read from device addresses in push data
			for (u32 i = 0; i < PushData::MAX_CBVS; i++)
			{
				VkDescriptorMappingSourceDataEXT data = {};
				data.pushAddressOffset = PushData::CBV_OFFSET + i * static_cast<u32>(sizeof(vk::DeviceAddress));
				add_mapping(BindingShift::CBV + i, 1, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT, VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT, data);
			}

			// t0-t15 / u0-u15: descriptor tables in the resource heap, table start slot in push data
			auto table = [this](u32 push_offset)
				{
					VkDescriptorMappingSourceDataEXT data = {};
					data.pushIndex.heapOffset = 0;
					data.pushIndex.pushOffset = push_offset;
					data.pushIndex.heapIndexStride = descriptor_slot_size;
					data.pushIndex.heapArrayStride = descriptor_slot_size;
					return data;
				};
			add_mapping(BindingShift::SRV, BindingShift::SAMPLER - BindingShift::SRV, srv_types, VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT, table(PushData::SRV_TABLE_OFFSET));
			add_mapping(BindingShift::UAV, BindingShift::END - BindingShift::UAV, uav_types, VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT, table(PushData::UAV_TABLE_OFFSET));

			// s0-s15: the global samplers, s<n> at sampler heap index n
			{
				VkDescriptorMappingSourceDataEXT data = {};
				data.constantOffset.heapOffset = 0;
				data.constantOffset.heapArrayStride = static_cast<u32>(heap_properties.samplerDescriptorSize);
				add_mapping(BindingShift::SAMPLER, kMaxSamplers, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT, VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT, data);
			}

			descriptor_mapping_info = { VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT };
			descriptor_mapping_info.mappingCount = static_cast<u32>(descriptor_mappings.size());
			descriptor_mapping_info.pMappings = descriptor_mappings.data();
		}

		void Device::create_null_descriptors()
		{
			// Without VK_EXT_robustness2 there are no null descriptors, so bindings the application leaves unset point at small
			// valid resources instead (D3D12 tolerates unset descriptors that are not read).
			SharedPtr<Memory> buffer_memory = allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64 * 1024);
			null_buffer = create_buffer(buffer_memory, 64 * 1024, BufferType::GeneralDataBuffer, true);

			const ResourceCreateDesc texture_desc(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
			const SizeAndAlignment size_align = calculate_resource_size_alignment(texture_desc);
			SharedPtr<Memory> texture_memory = allocate_memory(MemoryType::GPUOptimal, MemorySubType::Texture, size_align.size, size_align.alignment);
			null_texture = create_texture(texture_memory, static_cast<u32>(size_align.size), texture_desc);

			null_sampled_image_descriptor = std::vector<uint8_t>(null_texture->get_descriptor(null_texture->make_shader_resource_view_key(), vk::DescriptorType::eSampledImage),
				null_texture->get_descriptor(null_texture->make_shader_resource_view_key(), vk::DescriptorType::eSampledImage) + descriptor_slot_size);
			null_storage_buffer_descriptor.resize(descriptor_slot_size);
			const vk::DescriptorType type = vk::DescriptorType::eStorageBuffer;
			const vk::DeviceAddressRangeEXT range(null_buffer->get_device_address(), null_buffer->get_size_bytes());
			void* destination = null_storage_buffer_descriptor.data();
			write_buffer_descriptors(1, &type, &range, &destination);
		}

		vk::DeviceAddress Device::get_null_constant_buffer_address() const
		{
			return null_buffer->get_device_address();
		}

		const uint8_t* Device::get_null_descriptor(vk::DescriptorType type) const
		{
			switch (type)
			{
			case vk::DescriptorType::eSampledImage: return null_sampled_image_descriptor.data();
			case vk::DescriptorType::eStorageBuffer: return null_storage_buffer_descriptor.data();
			default: return nullptr; // No stand-in (storage images must be bound)
			}
		}

		u32 Device::allocate_descriptor_region()
		{
			std::lock_guard<std::mutex> lock(descriptor_region_mutex);
			seAssert(!free_descriptor_regions.empty(), "Out of resource heap regions for command lists");
			if (free_descriptor_regions.empty())
			{
				return 0;
			}
			const u32 first_slot = free_descriptor_regions.back();
			free_descriptor_regions.pop_back();
			return first_slot;
		}

		void Device::free_descriptor_region(u32 first_slot)
		{
			std::lock_guard<std::mutex> lock(descriptor_region_mutex);
			free_descriptor_regions.push_back(first_slot);
		}

		void Device::write_image_descriptor(void* destination, vk::DescriptorType type, const vk::ImageViewCreateInfo& view, vk::ImageLayout layout)
		{
			const VkImageViewCreateInfo& view_info = view;
			VkImageDescriptorInfoEXT image_info = { VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT };
			image_info.pView = &view_info;
			image_info.layout = static_cast<VkImageLayout>(layout);
			VkResourceDescriptorInfoEXT info = { VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
			info.type = static_cast<VkDescriptorType>(type);
			info.data.pImage = &image_info;
			VkHostAddressRangeEXT host_range = { destination, descriptor_slot_size };
			CHECKVK(device.getDispatcher()->vkWriteResourceDescriptorsEXT(static_cast<VkDevice>(*device), 1, &info, &host_range));
		}

		void Device::write_buffer_descriptors(u32 count, const vk::DescriptorType* types, const vk::DeviceAddressRangeEXT* ranges, void* const* destinations)
		{
			constexpr u32 kBatch = 64;
			VkResourceDescriptorInfoEXT infos[kBatch];
			VkHostAddressRangeEXT host_ranges[kBatch];
			for (u32 first = 0; first < count; first += kBatch)
			{
				const u32 batch = std::min(kBatch, count - first);
				for (u32 i = 0; i < batch; i++)
				{
					infos[i] = { VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
					infos[i].type = static_cast<VkDescriptorType>(types[first + i]);
					infos[i].data.pAddressRange = reinterpret_cast<const VkDeviceAddressRangeEXT*>(&ranges[first + i]);
					host_ranges[i] = { destinations[first + i], descriptor_slot_size };
				}
				CHECKVK(device.getDispatcher()->vkWriteResourceDescriptorsEXT(static_cast<VkDevice>(*device), batch, infos, host_ranges));
			}
		}

		void Device::imgui_init(u32, DXGI_FORMAT, DXGI_FORMAT, CommandQueue*)
		{
			// Frame count, formats and queue come from the swap chain and device (ImGui records into our command lists)
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
			const ResourceCreateDesc rcd(1024, 1024, DXGI_FORMAT_R8G8B8A8_UNORM);
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
				ImGui::Text("Wave Lane Count Min: %u", GetWaveLaneCountMin());
				ImGui::Text("Wave Lane Count Max: %u", GetWaveLaneCountMax());
				ImGui::EndDisabled();
				ImGui::SeparatorText("Descriptor Heaps");
				ImGui::Text("Descriptor slot size: %u bytes", descriptor_slot_size);
				ImGui::Text("Resource heap: %llu KiB (+%llu KiB reserved)", resource_heap->get_usable_size() / 1024llu, resource_heap->get_bind_info().reservedRangeSize / 1024llu);
				ImGui::Text("Sampler heap: %llu bytes (+%llu reserved)", sampler_heap->get_usable_size(), sampler_heap->get_bind_info().reservedRangeSize);
				ImGui::Text("Max push data: %llu bytes", heap_properties.maxPushDataSize);
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
			// ImGui records into our command buffer with its own pipeline and descriptor sets, so dynamic rendering must be
			// active and our shader, descriptor heap and push data state must be restored afterwards.
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
				const vk::BufferCreateInfo buffer_info({}, desc.width, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress);
				const vk::MemoryRequirements req = device.getBufferMemoryRequirements(vk::DeviceBufferMemoryRequirements(&buffer_info)).memoryRequirements;
				return { req.size, req.alignment };
			}
			vk::ImageCreateInfo image_info;
			image_info.imageType = desc.dimension == ResourceDimension::Texture1D ? vk::ImageType::e1D : (desc.dimension == ResourceDimension::Texture3D ? vk::ImageType::e3D : vk::ImageType::e2D);
			image_info.format = translate(desc.format);
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

		SharedPtr<GPUTimestampPool> Device::create_gpu_timestamp_pool(CommandQueue*, u32 max_timestamps)
		{
			const std::vector<vk::QueueFamilyProperties> families = physical_device.getQueueFamilyProperties();
			const u32 valid_bits = families[queue_family].timestampValidBits;
			seAssert(valid_bits > 0, "Queue does not support timestamps");
			return SharedPtr<GPUTimestampPool>(new GPUTimestampPool(device, properties.limits.timestampPeriod, valid_bits, max_timestamps));
		}

		SharedPtr<GPUStatisticPool> Device::create_gpu_statistic_pool(CommandQueue*, u32 max_stats)
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
			return SharedPtr<VertexShader>(new VertexShader(shader));
		}

		SharedPtr<PixelShader> Device::create_pixel_shader(const std::vector<uint8_t>& shader)
		{
			return SharedPtr<PixelShader>(new PixelShader(shader));
		}

		SharedPtr<ComputeShader> Device::create_compute_shader(const std::vector<uint8_t>& shader)
		{
			return SharedPtr<ComputeShader>(new ComputeShader(shader));
		}

		SharedPtr<MeshShader> Device::create_mesh_shader(const std::vector<uint8_t>& shader)
		{
			return mesh_shaders_supported ? SharedPtr<MeshShader>(new MeshShader(shader)) : nullptr;
		}

		SharedPtr<AmplificationShader> Device::create_amplification_shader(const std::vector<uint8_t>& shader)
		{
			return mesh_shaders_supported ? SharedPtr<AmplificationShader>(new AmplificationShader(shader)) : nullptr;
		}

		SharedPtr<GeometryShader> Device::create_geometry_shader(const std::vector<uint8_t>& shader)
		{
			return SharedPtr<GeometryShader>(new GeometryShader(shader));
		}

		void Device::create_shader_objects(Pipeline& pipeline, std::initializer_list<const Shader*> shader_list, bool mesh_without_task)
		{
			std::vector<const Shader*> shaders;
			for (const Shader* shader : shader_list)
			{
				if (shader)
				{
					seAssert(shader->is_valid(), "Invalid shader");
					shaders.push_back(shader);
				}
			}

			// Graphics stages are linked (the shader object equivalent of a monolithic pipeline) so the driver can optimise
			// across stage interfaces.
			const bool link = shaders.size() > 1;
			std::vector<VkShaderCreateInfoEXT> create_infos(shaders.size());
			for (size_t i = 0; i < shaders.size(); i++)
			{
				const Shader& shader = *shaders[i];
				VkShaderCreateInfoEXT& info = create_infos[i];
				info = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
				info.pNext = &descriptor_mapping_info;
				info.flags = VK_SHADER_CREATE_DESCRIPTOR_HEAP_BIT_EXT;
				if (link) info.flags |= VK_SHADER_CREATE_LINK_STAGE_BIT_EXT;
				if (shader.get_stage() == vk::ShaderStageFlagBits::eMeshEXT && mesh_without_task) info.flags |= VK_SHADER_CREATE_NO_TASK_SHADER_BIT_EXT;
				info.stage = static_cast<VkShaderStageFlagBits>(shader.get_stage());
				info.nextStage = i + 1 < shaders.size() ? static_cast<VkShaderStageFlags>(shaders[i + 1]->get_stage()) : 0;
				info.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
				info.codeSize = shader.get_code().size() * sizeof(u32);
				info.pCode = shader.get_code().data();
				info.pName = shader.get_entry_point();
			}

			std::vector<VkShaderEXT> handles(shaders.size(), VK_NULL_HANDLE);
			CHECKVK(device.getDispatcher()->vkCreateShadersEXT(static_cast<VkDevice>(*device), static_cast<u32>(create_infos.size()), create_infos.data(), nullptr, handles.data()));
			for (size_t i = 0; i < shaders.size(); i++)
			{
				pipeline.stages.push_back(shaders[i]->get_stage());
				pipeline.shaders.emplace_back(device, handles[i]);
			}

			// Record which descriptors the shaders use (table sizes, stand-ins for unset bindings)
			for (const Shader* shader : shaders)
			{
				for (const SpirvReflection::DescriptorBinding& b : shader->get_reflection().bindings)
				{
					seAssert(b.set == 0 && b.binding < BindingShift::END && b.count == 1, "Unsupported shader binding (set %u binding %u count %u)", b.set, b.binding, b.count);
					seAssert(b.binding >= BindingShift::SRV || b.binding < PushData::MAX_CBVS, "Constant buffer register b%u exceeds the push data CBVs", b.binding);
					const bool known = std::any_of(pipeline.descriptor_uses.begin(), pipeline.descriptor_uses.end(), [&b](const Pipeline::DescriptorUse& u) { return u.binding == b.binding; });
					if (!known)
					{
						pipeline.descriptor_uses.push_back({ b.binding, b.type });
					}
					if (b.binding >= BindingShift::SRV && b.binding < BindingShift::SAMPLER) pipeline.srv_table_size = std::max(pipeline.srv_table_size, b.binding - BindingShift::SRV + 1);
					if (b.binding >= BindingShift::UAV) pipeline.uav_table_size = std::max(pipeline.uav_table_size, b.binding - BindingShift::UAV + 1);
				}
			}
		}

		void Device::set_graphics_state(Pipeline& pipeline, const Rasterizer::Desc& rd, const DepthStencil::Desc& ds, const Blend::Desc& blend, u32 render_target_count)
		{
			pipeline.polygon_mode = translate(rd.fill_mode);
			pipeline.cull_mode = translate(rd.cull_mode);
			// The flipped (negative height) viewport keeps D3D's screen space winding, so D3D's convention maps directly
			pipeline.front_face = rd.front_counter_clockwise ? vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise;
			pipeline.depth_test = ds.depth_enable;
			pipeline.depth_write = ds.depth_enable && ds.depth_write;
			pipeline.depth_compare = translate(ds.comparison_function);
			pipeline.stencil_test = ds.stencil_enable;
			pipeline.stencil_front = translate_stencil(ds.stencil_front_desc, ds);
			pipeline.stencil_back = translate_stencil(ds.stencil_back_desc, ds);
			pipeline.alpha_to_coverage = blend.alpha_to_coverage_enable;
			pipeline.logic_op_enable = blend.render_targets[0].logic_op_enable && logic_op_supported;
			pipeline.logic_op = translate(blend.render_targets[0].logic_op);
			pipeline.render_target_count = render_target_count;
			for (u32 i = 0; i < render_target_count; i++)
			{
				const Blend::RenderTargetDesc& rt = blend.render_targets[blend.independent_blend_enable ? i : 0];
				pipeline.blend_enables.push_back(rt.blend_enable ? VK_TRUE : VK_FALSE);
				pipeline.blend_equations.push_back(vk::ColorBlendEquationEXT(translate(rt.src_blend), translate(rt.dest_blend), translate(rt.blend_op),
					translate(rt.src_blend_alpha), translate(rt.dest_blend_alpha), translate(rt.blend_op_alpha)));
				pipeline.write_masks.push_back(static_cast<vk::ColorComponentFlags>(rt.render_target_write_mask & 0xf));
			}
		}

		SharedPtr<Pipeline> Device::create_pipeline(const PipelineDesc::Graphics& pipeline_desc, const BindingDesc& binding_desc)
		{
			seAssert(pipeline_desc.vertex_shader != nullptr, "Graphics pipelines need a vertex shader");
			SharedPtr<Pipeline> pipeline(new Pipeline(binding_desc, vk::PipelineBindPoint::eGraphics));
			create_shader_objects(*pipeline, { pipeline_desc.vertex_shader.get(), pipeline_desc.geometry_shader.get(), pipeline_desc.pixel_shader.get() }, false);
			pipeline->has_vertex_shader = true;
			pipeline->topology = translate(pipeline_desc.primitive_topology);
			set_graphics_state(*pipeline, pipeline_desc.rasterizer_desc, pipeline_desc.depth_stencil_desc, pipeline_desc.blend_desc, pipeline_desc.render_target_count);

			// Vertex input: match input layout elements to the vertex shader's inputs by semantic (strides come from the vertex
			// buffer view at draw time)
			const SpirvReflection& vs_reflection = pipeline_desc.vertex_shader->get_reflection();
			const InputLayout::Desc& input_layout = pipeline_desc.input_layout;
			for (u32 i = 0; i < input_layout.num_elements; i++)
			{
				const InputLayout::ElementDesc& element = input_layout.elements[i];
				bool found = false;
				const u32 location = find_vertex_input_location(vs_reflection, element, found);
				if (!found)
				{
					continue; // Element not read by this vertex shader
				}
				seAssert(element.input_classification == InputLayout::InputClassification::PerVertex || element.instance_data_step_rate <= 1, "Instance step rates above 1 are not supported");
				const vk::VertexInputRate rate = element.input_classification == InputLayout::InputClassification::PerInstance ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex;
				pipeline->vertex_attributes.push_back({ location, element.input_slot, translate(element.format), element.aligned_byte_offset, rate });
			}
			for (const SpirvReflection::VertexInput& input : vs_reflection.vertex_inputs)
			{
				const bool provided = std::any_of(pipeline->vertex_attributes.begin(), pipeline->vertex_attributes.end(), [&input](const auto& a) { return a.location == input.location; });
				seAssert(provided, "Vertex shader input %s is not provided by the input layout", input.semantic.c_str());
				(void)provided;
			}
			return pipeline;
		}

		SharedPtr<Pipeline> Device::create_pipeline(const PipelineDesc::Compute& pipeline_desc, const BindingDesc& binding_desc)
		{
			seAssert(pipeline_desc.compute_shader != nullptr, "Compute pipelines need a compute shader");
			SharedPtr<Pipeline> pipeline(new Pipeline(binding_desc, vk::PipelineBindPoint::eCompute));
			create_shader_objects(*pipeline, { pipeline_desc.compute_shader.get() }, false);
			return pipeline;
		}

		SharedPtr<Pipeline> Device::create_pipeline(const PipelineDesc::Mesh& pipeline_desc, const BindingDesc& binding_desc)
		{
			if (!SupportsMeshShaders())
			{
				return nullptr;
			}
			seAssert(pipeline_desc.mesh_shader != nullptr, "No Mesh Shader Provided");
			seAssert(pipeline_desc.pixel_shader != nullptr, "No Pixel Shader Provided");
			SharedPtr<Pipeline> pipeline(new Pipeline(binding_desc, vk::PipelineBindPoint::eGraphics));
			create_shader_objects(*pipeline, { pipeline_desc.amp_shader.get(), pipeline_desc.mesh_shader.get(), pipeline_desc.pixel_shader.get() }, pipeline_desc.amp_shader == nullptr);
			pipeline->topology = translate(pipeline_desc.primitive_topology);
			set_graphics_state(*pipeline, pipeline_desc.rasterizer_desc, pipeline_desc.depth_stencil_desc, pipeline_desc.blend_desc, pipeline_desc.render_target_count);
			return pipeline;
		}

		SharedPtr<Buffer> Device::create_buffer(SharedPtr<Memory> memory, u32 size, BufferType type, bool uav_access)
		{
			// Buffers get every usage the engine might need (usage flags do not affect buffer performance on desktop GPUs)
			const vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
				vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
				vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
			vk::raii::Buffer vk_buffer(device, vk::BufferCreateInfo({}, size, usage, vk::SharingMode::eExclusive));

			const vk::MemoryRequirements requirements = vk_buffer.getMemoryRequirements();
			seAssert(requirements.size <= memory->get_size(), "Buffer does not fit in its memory");
			seAssert((requirements.memoryTypeBits & (1u << memory->get_memory_type_index())) != 0, "Memory type not compatible with buffer");
			CHECKVK(vmaBindBufferMemory(allocator, memory->get_allocation(), static_cast<VkBuffer>(*vk_buffer)));

			const bool upload = memory->get_type() == MemoryType::Upload;
			const bool readback = memory->get_type() == MemoryType::Readback;
			SharedPtr<Buffer> buffer(new Buffer(type, size, uav_access, upload, readback));
			buffer->memory = memory;
			buffer->device_address = device.getBufferAddress(vk::BufferDeviceAddressInfo(*vk_buffer));
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
			cbv.address = buffer->get_device_address() + offset;
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

		vk::SamplerCreateInfo Device::make_sampler_create_info(const sg::SamplerDesc& desc) const
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
			return vk::SamplerCreateInfo({}, mag_filter, min_filter, mip_mode, translate(desc.address_u), translate(desc.address_v), translate(desc.address_w),
				std::clamp(desc.mip_lod_bias, -max_bias, max_bias), anisotropic ? VK_TRUE : VK_FALSE, max_anisotropy, compare ? VK_TRUE : VK_FALSE,
				translate(desc.comparison_func), desc.min_lod, desc.max_lod, translate_border_colour(desc.border_color));
		}

		void Device::write_samplers()
		{
			// Sampler s<n> lives at sampler heap index n. Rewritten only between frames (no command buffer is executing).
			if (samplers.empty())
			{
				return;
			}
			std::vector<VkSamplerCreateInfo> infos;
			std::vector<VkHostAddressRangeEXT> destinations;
			for (size_t i = 0; i < samplers.size(); i++)
			{
				infos.push_back(make_sampler_create_info(samplers[i].sampler_desc));
				destinations.push_back({ sampler_heap->get_mapped_data() + i * heap_properties.samplerDescriptorSize, static_cast<size_t>(heap_properties.samplerDescriptorSize) });
			}
			CHECKVK(device.getDispatcher()->vkWriteSamplerDescriptorsEXT(static_cast<VkDevice>(*device), static_cast<u32>(infos.size()), infos.data(), destinations.data()));
		}

		void Device::AddSamplers(const char* sampler_name, const sg::SamplerDesc& sampler_desc)
		{
			seAssert(samplers.size() < kMaxSamplers, "Too many global samplers");
			samplers.push_back({ sampler_name, sampler_desc });
			write_samplers();
		}

		void Device::RecreateSamplers()
		{
			// Called from ImGui between frames (the previous frame has completed)
			write_samplers();
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
			seWriteLine("Vulkan: recreating swap chain");
			device.waitIdle();
			// Old images are about to be destroyed; drop any first-use transitions still queued for them
			for (const SharedPtr<Texture>& texture : swap_chain_textures)
			{
				remove_pending_initial_transition(texture->get());
			}
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
				remove_pending_initial_transition(texture->get());
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
