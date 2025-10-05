#include "pch.h"
#include "sgD3D12Device.h"
#include "sgD3D12CommandQueue.h"
#include "sgD3D12CommandList.h"
#include "sgD3D12RenderTargetView.h"
#include "sgD3D12ConstantBufferView.h"
#include "sgD3D12VertexBufferView.h"
#include "sgD3D12IndexBufferView.h"
#include "sgD3D12Pipeline.h"
#include "sgD3D12TypesTranslator.h"
#include "sgD3D12GPUTimestampPool.h"
#include "sgD3D12GPUStatisticPool.h"
#include <imgui_impl_dx12.h>
#include "sgUtils.h"

namespace sg
{
	namespace D3D12
	{
        // Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
        // If no such adapter can be found, *ppAdapter will be set to nullptr.
        void GetHardwareAdapter(IDXGIFactory1* pFactory, IDXGIAdapter1** ppAdapter, bool requestHighPerformanceAdapter, D3D_FEATURE_LEVEL feature_level)
        {
            *ppAdapter = nullptr;

            ComPtr<IDXGIAdapter1> adapter;

            ComPtr<IDXGIFactory6> factory6;
            if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
            {
                for (
                    UINT adapterIndex = 0;
                    SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                        adapterIndex,
                        requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                        IID_PPV_ARGS(&adapter)));
                        ++adapterIndex)
                {
                    DXGI_ADAPTER_DESC1 desc;
                    adapter->GetDesc1(&desc);

                    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    {
                        // Don't select the Basic Render Driver adapter.
                        // If you want a software adapter, pass in "/warp" on the command line.
                        continue;
                    }

                    // Check to see whether the adapter supports Direct3D 12, but don't create the
                    // actual device yet.
                    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), feature_level, _uuidof(ID3D12Device), nullptr)))
                    {
                        break;
                    }
                }
            }

            if (adapter.Get() == nullptr)
            {
                for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
                {
                    DXGI_ADAPTER_DESC1 desc;
                    adapter->GetDesc1(&desc);

                    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    {
                        // Don't select the Basic Render Driver adapter.
                        // If you want a software adapter, pass in "/warp" on the command line.
                        continue;
                    }

                    // Check to see whether the adapter supports Direct3D 12, but don't create the
                    // actual device yet.
                    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), feature_level, _uuidof(ID3D12Device), nullptr)))
                    {
                        break;
                    }
                }
            }

            *ppAdapter = adapter.Detach();
        }
        
        void d3d12_message_callback(D3D12_MESSAGE_CATEGORY Category, D3D12_MESSAGE_SEVERITY Severity, D3D12_MESSAGE_ID ID, LPCSTR pDescription, void* pContext)
        {
            __debugbreak();
        }

        void setup_debug_filters(ID3D12Device* device)
        {
#if false//_DEBUG
            ID3D12InfoQueue* pInfoQueue = nullptr;
            if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&pInfoQueue))))
            {
                D3D12_INFO_QUEUE_FILTER NewFilter = {};

                D3D12_MESSAGE_SEVERITY Severities[] =
                {
                    D3D12_MESSAGE_SEVERITY_CORRUPTION,
                    D3D12_MESSAGE_SEVERITY_ERROR,
                    D3D12_MESSAGE_SEVERITY_WARNING,
                    //D3D12_MESSAGE_SEVERITY_INFO,
                    //D3D12_MESSAGE_SEVERITY_MESSAGE,
                };
                NewFilter.AllowList.NumSeverities = _countof(Severities);
                NewFilter.AllowList.pSeverityList = Severities;

                pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
                pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
                pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
                pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, false);
                //CHECKHR(pInfoQueue->RegisterMessageCallback(d3d12_message_callback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, nullptr));
                pInfoQueue->PushStorageFilter(&NewFilter);
                pInfoQueue->Release();
            }
#endif
        }

		Device::Device()
		{
            seWriteLine("Creating D3D12 Device...");

            const D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_12_0;
            UINT dxgiFactoryFlags = 0;

            #if defined(_DEBUG)
                // Enable the debug layer (requires the Graphics Tools "optional feature").
                // NOTE: Enabling the debug layer after device creation will invalidate the active device.
                {
                    ComPtr<ID3D12Debug> debugController;
                    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
                    {
                        debugController->EnableDebugLayer();

                        // Enable additional debug layers.
                        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

                        #if false
						    ComPtr<ID3D12Debug1> debugController1;
						    if (SUCCEEDED(debugController->QueryInterface(IID_PPV_ARGS(&debugController1))))
						    {
                                debugController1->SetEnableGPUBasedValidation(true);
						    }
                        #endif
                    }

                }
            #endif

            CHECKHR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

            ComPtr<IDXGIAdapter1> hardware_adapter;
            GetHardwareAdapter(factory.Get(), &hardware_adapter, true, feature_level);
			
            //DXGI Info
            {
				hardware_adapter->QueryInterface(IID_PPV_ARGS(&hardware_adapter_4));

                adapter_description = {};
				CHECKHR(hardware_adapter_4->GetDesc3(&adapter_description));
				seWriteLine("%ls", adapter_description.Description);

                DXGI_QUERY_VIDEO_MEMORY_INFO memory_information = {};
                CHECKHR(hardware_adapter_4->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory_information));
                seWriteLine("DXGI_QUERY_VIDEO_MEMORY_INFO:");
                seWriteLine(" - Budget: %lluMiB", memory_information.Budget / 1024llu / 1024llu); //Specifies the OS-provided video memory budget, in bytes, that the application should target. If CurrentUsage is greater than Budget, the application may incur stuttering or performance penalties due to background activity by the OS to provide other applications with a fair usage of video memory.
				seWriteLine(" - CurrentUsage: %lluMiB", memory_information.CurrentUsage / 1024llu / 1024llu); //Specifies the application's current video memory usage, in bytes.
				seWriteLine(" - AvailableForReservation: %lluMiB", memory_information.AvailableForReservation / 1024llu / 1024llu); //The amount of video memory, in bytes, that the application has available for reservation. To reserve this video memory, the application should call IDXGIAdapter3::SetVideoMemoryReservation.
				seWriteLine(" - CurrentReservation: %lluMiB", memory_information.CurrentReservation / 1024llu / 1024llu); //The amount of video memory, in bytes, that is reserved by the application. The OS uses the reservation as a hint to determine the application’s minimum working set. Applications should attempt to ensure that their video memory usage can be trimmed to meet this requirement.
			}

            CHECKHR(D3D12CreateDevice(hardware_adapter.Get(), feature_level, IID_PPV_ARGS(&device)));
            CHECKHR(device->QueryInterface(IID_PPV_ARGS(&device6)));
            seWriteLine("D3D12 Device created.");

#if defined(_DEBUG)
			{
                HRESULT hr_power_state = device6->SetStablePowerState(true);
                CHECKHR(hr_power_state);
				seWriteLine("D3D12 SetStablePowerState: %s", hr_power_state == S_OK ? "ENABLED" : "FAILED");
			}
#endif

            setup_debug_filters(device.Get());

            features.Init(device.Get());
           
            create_descriptors();

            //Create memory allocators
            {
                memory_manager.desc = {};
                memory_manager.desc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;
                memory_manager.desc.pDevice = device.Get();
                memory_manager.desc.pAdapter = hardware_adapter.Get();
                CHECKHR(D3D12MA::CreateAllocator(&memory_manager.desc, &memory_manager.allocator));

                D3D12MA::POOL_DESC pool_desc = {};
                pool_desc.HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
				pool_desc.HeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
             
                CHECKHR(memory_manager.allocator->CreatePool(&pool_desc, &memory_manager.pools[(u32)MemoryManager::PoolTypes::Textures]));

				pool_desc.HeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
				CHECKHR(memory_manager.allocator->CreatePool(&pool_desc, &memory_manager.pools[(u32)MemoryManager::PoolTypes::Targets]));

				pool_desc.HeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
				CHECKHR(memory_manager.allocator->CreatePool(&pool_desc, &memory_manager.pools[(u32)MemoryManager::PoolTypes::Buffers]));

				pool_desc.HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
				pool_desc.HeapFlags = D3D12_HEAP_FLAG_NONE | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
				CHECKHR(memory_manager.allocator->CreatePool(&pool_desc, &memory_manager.pools[(u32)MemoryManager::PoolTypes::Upload]));

                pool_desc.HeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
				pool_desc.HeapFlags = D3D12_HEAP_FLAG_NONE | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
				CHECKHR(memory_manager.allocator->CreatePool(&pool_desc, &memory_manager.pools[(u32)MemoryManager::PoolTypes::Readback]));
            }
            {
                seWriteLine("Created all allocators.");
            }
		}

        void Device::imgui_init(u32 num_frames, DXGI_FORMAT format, DXGI_FORMAT depth_format, CommandQueue* command_queue)
        {
            ImGui_ImplDX12_InitInfo init_info = {};
            init_info.Device = device.Get();
            init_info.CommandQueue = command_queue->get().Get();
            init_info.NumFramesInFlight = num_frames;
            init_info.RTVFormat = format;
            init_info.DSVFormat = depth_format;
            init_info.SrvDescriptorHeap = cbv_srv_uav_descriptor_heap_imgui->get_heap().Get();
            init_info.UserData = this;
            init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
                {
                    Device* device = (Device*)info->UserData;

					u32 idx = device->cbv_srv_uav_descriptor_heap_imgui->allocate();
					CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_handle(device->cbv_srv_uav_descriptor_heap_imgui->get_cpu_handle_heap_start(), idx, device->cbv_srv_uav_descriptor_heap_imgui->get_increment_size());
					CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(device->cbv_srv_uav_descriptor_heap_imgui->get_gpu_handle_heap_start(), idx, device->cbv_srv_uav_descriptor_heap_imgui->get_increment_size());
                    *out_cpu_desc_handle = cpu_handle;
                    *out_gpu_desc_handle = gpu_handle;
                };
            init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle)
                {
					Device* device = (Device*)info->UserData;
                    seAssert(false, "TODO");
                };

            // Custom ImGui Texture
			{
				imgui_texture_viewer = {};
				u32 idx = cbv_srv_uav_descriptor_heap_imgui->allocate();
				imgui_texture_viewer.cpu_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(cbv_srv_uav_descriptor_heap_imgui->get_cpu_handle_heap_start(), idx, cbv_srv_uav_descriptor_heap_imgui->get_increment_size());
				imgui_texture_viewer.gpu_handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(cbv_srv_uav_descriptor_heap_imgui->get_gpu_handle_heap_start(), idx, cbv_srv_uav_descriptor_heap_imgui->get_increment_size());
                ResourceCreateDesc rcd = {};
                rcd.width = 1024;
                rcd.height = 1024;
                rcd.format = DXGI_FORMAT_R8G8B8A8_UNORM;
                //rcd.usage_flags = ResourceUsageFlags::RenderTarget | ResourceUsageFlags::UnorderedAccess;
				sg::SizeAndAlignment size_align = calculate_resource_size_alignment(rcd);
                imgui_texture_viewer.texture_memory = allocate_memory(MemoryType::GPUOptimal, MemorySubType::Texture, size_align.size, size_align.alignment);
                imgui_texture_viewer.texture = create_texture(imgui_texture_viewer.texture_memory, size_align.size, rcd);
                imgui_texture_viewer.texture->get()->SetName(L"ImGui View Texture");
                CD3DX12_SHADER_RESOURCE_VIEW_DESC srvd = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(rcd.format);
                device->CreateShaderResourceView(imgui_texture_viewer.texture->get().Get(), &srvd, imgui_texture_viewer.cpu_handle);
			}
            

            if (!ImGui_ImplDX12_Init(&init_info))
            {
                seAssert(false, "Error: ImGui_ImplDX12_Init failed!" );
            }
        }


		void Device::imgui_update()
		{
            ImGui::PushID("SGDevice");

            if (ImGui::CollapsingHeader("Device"))
            {
                ImGui::Text("Adapter ID: %S", adapter_description.Description);
				ImGui::Text("Dedicated Video Memory: %llu (MiB)", adapter_description.DedicatedVideoMemory / 1024llu / 1024llu);
				ImGui::Text("Dedicated System Memory: %llu (MiB)", adapter_description.DedicatedSystemMemory / 1024llu / 1024llu);
				ImGui::Text("Shared System Memory: %llu (MiB)", adapter_description.SharedSystemMemory / 1024llu / 1024llu);
                ImGui::SeparatorText("Feature Support");
                ImGui::BeginDisabled();
				ImGui::RadioButton("UMA", features.UMA());
				ImGui::RadioButton("Cache Coherent UMA", features.CacheCoherentUMA());
				ImGui::RadioButton("Tile Based Renderer", features.TileBasedRenderer());
				ImGui::RadioButton("Isolated MMU", features.IsolatedMMU());

				ImGui::RadioButton("Ray Tracing", features.RaytracingTier() != D3D12_RAYTRACING_TIER_NOT_SUPPORTED);
                ImGui::RadioButton("Mesh Shaders", SupportsMeshShaders());
				ImGui::RadioButton("Wave Operations", SupportsWaveOps());
				ImGui::Text("Wave Lane Count Min: %u", GetWaveLaneCountMin());
				ImGui::Text("Wave Lane Count Max: %u", GetWaveLaneCountMax());
				ImGui::Text("Wave Total Lane Count: %u", GetTotalLaneCount());
                ImGui::EndDisabled();
            }
            if (ImGui::CollapsingHeader("Memory"))
            {
				DXGI_QUERY_VIDEO_MEMORY_INFO memory_information = {};
				CHECKHR(hardware_adapter_4->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory_information));

				ImGui::SeparatorText("Adapter Video Memory (MiB):");
                ImGui::Text(" - Budget: %llu", memory_information.Budget / 1024llu / 1024llu); //Specifies the OS-provided video memory budget, in bytes, that the application should target. If CurrentUsage is greater than Budget, the application may incur stuttering or performance penalties due to background activity by the OS to provide other applications with a fair usage of video memory.
                ImGui::Text(" - CurrentUsage: %llu", memory_information.CurrentUsage / 1024llu / 1024llu); //Specifies the application's current video memory usage, in bytes.
                ImGui::Text(" - AvailableForReservation: %llu", memory_information.AvailableForReservation / 1024llu / 1024llu); //The amount of video memory, in bytes, that the application has available for reservation. To reserve this video memory, the application should call IDXGIAdapter3::SetVideoMemoryReservation.
                ImGui::Text(" - CurrentReservation: %llu", memory_information.CurrentReservation / 1024llu / 1024llu); //The amount of video memory, in bytes, that is reserved by the application. The OS uses the reservation as a hint to determine the application’s minimum working set. Applications should attempt to ensure that their video memory usage can be trimmed to meet this requirement.

                ImGui::SeparatorText("D3D12MA Pools:");
                D3D12MA::Budget local, non_local;
                memory_manager.allocator->GetBudget(&local, &non_local);
                ImGui::Text("Total Usage: %llu (MiB)", local.UsageBytes / 1024llu / 1024llu);
				ImGui::Text("Budget: %llu (MiB)", local.BudgetBytes / 1024llu / 1024llu);

                ImGui::Text("Pools:");
                for (size_t i = 0; i < (u32)MemoryManager::PoolTypes::Max; i++)
                {
                    D3D12MA::Pool* pool = memory_manager.pools[i];

					D3D12MA::Statistics stats;
					pool->GetStatistics(&stats);

                    ImGui::Text("%10s: %llu/%llu (MiB)", MemoryManager::PoolTypeNames[i], stats.AllocationBytes / 1024llu / 1024llu, stats.BlockBytes / 1024llu / 1024llu);
                }
                ImGui::Indent();
                if (ImGui::CollapsingHeader("Detailed Memory Information"))
                {
                    ImGui::Text("Viewing this information is slow to be calculated!");
                    D3D12MA::TotalStatistics total_stats;
                    memory_manager.allocator->CalculateStatistics(&total_stats);

                    ImGui::Text("Pools:");
                    for (size_t i = 0; i < (u32)MemoryManager::PoolTypes::Max; i++)
                    {
                        D3D12MA::Pool* pool = memory_manager.pools[i];

                        D3D12MA::DetailedStatistics stats;
                        pool->CalculateStatistics(&stats);
                        ImGui::SeparatorText(MemoryManager::PoolTypeNames[i]);
                        ImGui::Text("- Memory (KiB): %llu/%llu", stats.Stats.AllocationBytes / 1024llu, stats.Stats.BlockBytes / 1024llu);
                        ImGui::Text("- Block Count: %llu AllocationCount: %llu", stats.Stats.BlockCount, stats.Stats.AllocationCount);
                        ImGui::Text("- AllocationSizeMin (KiB): %llu AllocationSizeMax (KiB): %llu", stats.AllocationSizeMin / 1024llu, stats.AllocationSizeMax / 1024llu);
						ImGui::Text("- UnusedRangeSizeMin (KiB): %llu UnusedRangeSizeMax (KiB): %llu", stats.UnusedRangeSizeMin / 1024llu, stats.UnusedRangeSizeMax / 1024llu);
                    }
                }
                ImGui::Unindent();
            }

            ImGui::PopID();            
		}

		void Device::imgui_render(CommandList* command_list)
        {
            ID3D12GraphicsCommandList6* d3d_cmd_list = command_list->get().Get();

            ID3D12DescriptorHeap* heap = cbv_srv_uav_descriptor_heap_imgui->get_heap().Get();
            d3d_cmd_list->SetDescriptorHeaps(1, &heap);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3d_cmd_list);

#ifdef SE_IMGUI_DOCKING
			// Update and Render additional Platform Windows
			if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				ImGui::UpdatePlatformWindows();
				ImGui::RenderPlatformWindowsDefault();
			}
#endif
        }


		void Device::set_imgui_viewer_texture(SharedPtr<Texture> texture)
		{
            imgui_texture_viewer.texture = texture;
			CD3DX12_SHADER_RESOURCE_VIEW_DESC srvd = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(texture->resource_create_desc.format);
			device->CreateShaderResourceView(imgui_texture_viewer.texture->get().Get(), &srvd, imgui_texture_viewer.cpu_handle);
		}

		D3D12_RESOURCE_DESC Device::create_dx12_resource_desc(const ResourceCreateDesc& desc)
		{
			const ResourceUsageFlags invalid_4kb_flags = ResourceUsageFlags::RenderTarget | ResourceUsageFlags::DepthStencil;
			const bool bAlign4Kb = false;// desc.try_alignment_4kb && (static_cast<u32>(desc.usage_flags & invalid_4kb_flags) == 0) && desc.dimension != ResourceDimension::Buffer; //SEE HOW D3D12MA DOES THIS BUT IT'S NOT EXPOSED VIA HEADER

			D3D12_RESOURCE_DESC d3d12_desc = {};
			d3d12_desc.Alignment = bAlign4Kb ? 4096 : 0; //0 = default
			d3d12_desc.Dimension = translate(desc.dimension);
			d3d12_desc.Width = desc.width;
			d3d12_desc.Height = desc.height;
			d3d12_desc.DepthOrArraySize = desc.depth;
			d3d12_desc.MipLevels = desc.mip_count;
			d3d12_desc.Format = desc.format;
			d3d12_desc.SampleDesc.Count = 1;
			d3d12_desc.SampleDesc.Quality = 0;
			d3d12_desc.Layout = desc.dimension == ResourceDimension::Buffer ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN;
			d3d12_desc.Flags = translate(desc.usage_flags);

            return d3d12_desc;
		}

		SizeAndAlignment Device::calculate_resource_size_alignment(const ResourceCreateDesc& desc)
		{
            D3D12_RESOURCE_DESC d3d12_desc = create_dx12_resource_desc(desc);

            D3D12_RESOURCE_ALLOCATION_INFO alloc_info = device->GetResourceAllocationInfo(0, 1, &d3d12_desc);
            return { alloc_info.SizeInBytes, alloc_info.Alignment };
		}

		SharedPtr<Memory> Device::allocate_memory(MemoryType type, MemorySubType sub_type, u64 size, u64 alignment)
        {
            size = AlignUp(size, alignment);
            if (type == MemoryType::Upload)
            {
				D3D12MA::ALLOCATION_DESC ad = {};
				ad.CustomPool = memory_manager.pools[(u32)MemoryManager::PoolTypes::Upload];
				D3D12_RESOURCE_ALLOCATION_INFO alloc_info{ size, alignment };
				D3D12MA::Allocation* alloc;
				CHECKHR(memory_manager.allocator->AllocateMemory(&ad, &alloc_info, &alloc))
				return SharedPtr<Memory>(new Memory(type, alloc));
            }
            else if (type == MemoryType::Readback)
			{
				D3D12MA::ALLOCATION_DESC ad = {};
				ad.CustomPool = memory_manager.pools[(u32)MemoryManager::PoolTypes::Readback];
				D3D12_RESOURCE_ALLOCATION_INFO alloc_info{ size, alignment };
				D3D12MA::Allocation* alloc;
				CHECKHR(memory_manager.allocator->AllocateMemory(&ad, &alloc_info, &alloc))
				return SharedPtr<Memory>(new Memory(type, alloc));
            }
            else //if (type == MemoryType::GPUOptimal)
            {
                D3D12MA::Pool* pool = nullptr;
                switch (sub_type)
                {
                case MemorySubType::Texture: pool = memory_manager.pools[(u32)MemoryManager::PoolTypes::Textures]; break;
                case MemorySubType::Target: pool = memory_manager.pools[(u32)MemoryManager::PoolTypes::Targets]; break;
                case MemorySubType::Buffer: pool = memory_manager.pools[(u32)MemoryManager::PoolTypes::Buffers]; break;
                default:
                    seAssert(false, "Missing Pool Type");
                }

                D3D12MA::ALLOCATION_DESC ad = {};
                ad.CustomPool = pool;
                D3D12_RESOURCE_ALLOCATION_INFO alloc_info{ size, alignment };
                D3D12MA::Allocation* alloc;
                CHECKHR(memory_manager.allocator->AllocateMemory(&ad, &alloc_info, &alloc));

				return SharedPtr<Memory>(new Memory(type, alloc));
            }
        }

        ComPtr<QueueFence> Device::create_queue_fence()
        {
            ComPtr<QueueFence> fence;
            CHECKHR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())))
            return fence;
        }


        SharedPtr<GPUTimestampPool> Device::create_gpu_timestamp_pool(CommandQueue* queue, u32 max_timestamps)
		{
            ID3D12CommandQueue* d3d12_queue = queue->get().Get();
            return SharedPtr<GPUTimestampPool>(new GPUTimestampPool(device.Get(), d3d12_queue, max_timestamps));
		}


        SharedPtr<sg::D3D12::GPUStatisticPool> Device::create_gpu_statistic_pool(CommandQueue* queue, u32 max_stats)
		{
			ID3D12CommandQueue* d3d12_queue = queue->get().Get();
			return SharedPtr<GPUStatisticPool>(new GPUStatisticPool(device6.Get(), d3d12_queue, max_stats));
		}

        SharedPtr<CommandQueue> Device::create_command_queue()
        {
            ComPtr<ID3D12CommandQueue> queue;
            D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
            QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            CHECKHR(device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&queue)));
            CHECKHR(queue->SetName(L"CommandQueue"));
            return Ptr<CommandQueue>(new CommandQueue(queue));
        }

        SharedPtr<CommandList> Device::create_command_buffer()
        {
            ComPtr<ID3D12GraphicsCommandList6> command_list;
            ComPtr<ID3D12CommandAllocator> command_allocator;

            CHECKHR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator)));
            CHECKHR(device6->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator.Get(), nullptr, IID_PPV_ARGS(&command_list)));
            CHECKHR(command_list->Close());

            SharedPtr<CommandList> out_command_list(new CommandList(command_list, command_allocator));
            //cbv srv uav
            {
                ComPtr<ID3D12DescriptorHeap> heap;
                D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
                cbvSrvHeapDesc.NumDescriptors = DESCRIPTOR_COUNT;
                cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                CHECKHR(device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&heap)));
                heap->SetName(L"Command List: CBV SRV UAV Heap");
                u32 increment_size = device->GetDescriptorHandleIncrementSize(cbvSrvHeapDesc.Type);
                out_command_list->descriptor_heap_increment = increment_size;
                out_command_list->descriptor_heap_maximum = cbvSrvHeapDesc.NumDescriptors;
                out_command_list->descriptor_heap = heap;

				cbvSrvHeapDesc.NumDescriptors = 1;
				cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				CHECKHR(device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&heap)));
                out_command_list->non_shader_descriptor_heap = heap;
            }

            out_command_list->device = device;
            out_command_list->sg_device = this;
            
            out_command_list->descriptor_increment_size_cbv_srv_uav = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            out_command_list->descriptor_increment_size_rtv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            out_command_list->descriptor_increment_size_dsv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            out_command_list->descriptor_increment_size_sampler = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

            return out_command_list;
        }

        SharedPtr<VertexShader> Device::create_vertex_shader(const std::vector<uint8_t>& shader)
        {
            return SharedPtr<VertexShader>(new VertexShader(shader));
        }

        SharedPtr<PixelShader> Device::create_pixel_shader(const std::vector<uint8_t>& shader)
        {
            return SharedPtr<PixelShader>(new PixelShader(shader));
        }

		sg::SharedPtr<sg::ComputeShader> Device::create_compute_shader(const std::vector<uint8_t>& shader)
		{
			return SharedPtr<ComputeShader>(new ComputeShader(shader));
		}


		sg::SharedPtr<sg::MeshShader> Device::create_mesh_shader(const std::vector<uint8_t>& shader)
		{
			return SharedPtr<MeshShader>(new MeshShader(shader));
		}


		sg::SharedPtr<sg::AmplificationShader> Device::create_amplification_shader(const std::vector<uint8_t>& shader)
		{
			return SharedPtr<AmplificationShader>(new AmplificationShader(shader));
		}

        SharedPtr<Pipeline> Device::create_pipeline(const PipelineDesc::Graphics& pipeline_desc, const BindingDesc& binding_desc)
        {
            SharedPtr<Pipeline> out_pipeline(new Pipeline(pipeline_desc));

            out_pipeline->topology = translate(pipeline_desc.primitive_topology);

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            {
                psoDesc.VS = pipeline_desc.vertex_shader ? pipeline_desc.vertex_shader->shader_code : CD3DX12_SHADER_BYTECODE();
                psoDesc.PS = pipeline_desc.pixel_shader ? pipeline_desc.pixel_shader->shader_code : CD3DX12_SHADER_BYTECODE();
                psoDesc.pRootSignature = nullptr;
                psoDesc.PrimitiveTopologyType = translate(pipeline_desc.topology);
                psoDesc.BlendState = translate(pipeline_desc.blend_desc);
                psoDesc.DepthStencilState = translate(pipeline_desc.depth_stencil_desc);
                psoDesc.RasterizerState = translate(pipeline_desc.rasterizer_desc);
                psoDesc.SampleMask = UINT_MAX;
                psoDesc.NumRenderTargets = pipeline_desc.render_target_count;
                for (u32 i = 0; i < pipeline_desc.render_target_count; i++)
                {
                    psoDesc.RTVFormats[i] = pipeline_desc.render_target_format_list[i];
                }
                psoDesc.DSVFormat = pipeline_desc.depth_stencil_format;
                psoDesc.SampleDesc.Count = 1;
            }

            D3D12_INPUT_ELEMENT_DESC ied_array[sg::InputLayout::Desc::MAX_ELEMENTS] = {};
            D3D12_INPUT_LAYOUT_DESC ild = {};
            if (pipeline_desc.input_layout.num_elements > 0)
            {
				seAssert(pipeline_desc.input_layout.num_elements <= sg::InputLayout::Desc::MAX_ELEMENTS, "Too many elements");
                ild.pInputElementDescs = ied_array;
                ild.NumElements = pipeline_desc.input_layout.num_elements;
                for (size_t i = 0; i < pipeline_desc.input_layout.num_elements; i++)
                {
                    D3D12_INPUT_ELEMENT_DESC& ied = ied_array[i];
                    const sg::InputLayout::ElementDesc& ed = pipeline_desc.input_layout.elements[i];

                    ied.SemanticName = ed.semantic_name;
                    ied.SemanticIndex = ed.semantic_index;
                    ied.Format = ed.format;
                    ied.InputSlot = ed.input_slot;
                    ied.AlignedByteOffset = ed.aligned_byte_offset;
                    ied.InputSlotClass = translate(ed.input_classification);
                    ied.InstanceDataStepRate = ed.instance_data_step_rate;
                }
                psoDesc.InputLayout = ild;
            }
            
            //Root Signature Generation
            {
                out_pipeline->root_signature = create_root_signature(binding_desc, false);
                psoDesc.pRootSignature = out_pipeline->root_signature.Get();
            }

            CHECKHR(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(out_pipeline->pipeline.GetAddressOf())));
            return out_pipeline;
        }


        SharedPtr<Pipeline> Device::create_pipeline(const PipelineDesc::Compute& pipeline_desc, const BindingDesc& binding_desc)
		{
            SharedPtr<Pipeline> out_pipeline(new Pipeline(pipeline_desc));

			D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.CS = pipeline_desc.compute_shader->shader_code;

			//Root Signature Generation
			{
				out_pipeline->root_signature = create_root_signature(binding_desc, true);
				psoDesc.pRootSignature = out_pipeline->root_signature.Get();
			}

			CHECKHR(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(out_pipeline->pipeline.GetAddressOf())));
            
            return out_pipeline;
		}

        SharedPtr<Pipeline> Device::create_pipeline(const PipelineDesc::Mesh& pipeline_desc, const BindingDesc& binding_desc)
		{
            if (!SupportsMeshShaders())
            {
                return nullptr;
            }

            SharedPtr<Pipeline> out_pipeline(new Pipeline(pipeline_desc));

			out_pipeline->topology = translate(pipeline_desc.primitive_topology);

            seAssert(pipeline_desc.mesh_shader != nullptr, "No Mesh Shader Provided");
            seAssert(pipeline_desc.pixel_shader != nullptr, "No Pixel Shader Provided");

			D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.pRootSignature = nullptr;
			psoDesc.AS = pipeline_desc.amp_shader ? pipeline_desc.amp_shader->shader_code : CD3DX12_SHADER_BYTECODE();
            psoDesc.MS = pipeline_desc.mesh_shader->shader_code;
            psoDesc.PS = pipeline_desc.pixel_shader->shader_code;
			psoDesc.PrimitiveTopologyType = translate(pipeline_desc.topology);
			psoDesc.BlendState = translate(pipeline_desc.blend_desc);
			psoDesc.DepthStencilState = translate(pipeline_desc.depth_stencil_desc);
			psoDesc.RasterizerState = translate(pipeline_desc.rasterizer_desc);
			psoDesc.SampleMask = UINT_MAX;
			psoDesc.NumRenderTargets = pipeline_desc.render_target_count;
			for (u32 i = 0; i < pipeline_desc.render_target_count; i++)
			{
				psoDesc.RTVFormats[i] = pipeline_desc.render_target_format_list[i];
			}
			psoDesc.DSVFormat = pipeline_desc.depth_stencil_format;
			psoDesc.SampleDesc.Count = 1;

			//Root Signature Generation
			{
				out_pipeline->root_signature = create_root_signature(binding_desc, false);
				psoDesc.pRootSignature = out_pipeline->root_signature.Get();
			}

			auto psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);
			D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
			streamDesc.pPipelineStateSubobjectStream = &psoStream;
			streamDesc.SizeInBytes = sizeof(psoStream);

            CHECKHR(device6->CreatePipelineState(&streamDesc, IID_PPV_ARGS(out_pipeline->pipeline.GetAddressOf())));
			return out_pipeline;
		}

		sg::SharedPtr<sg::Buffer> Device::create_buffer(SharedPtr<Memory> memory, u32 size, BufferType type, bool uav_access)
		{
            const D3D12_RESOURCE_FLAGS flags = uav_access ? (D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) : D3D12_RESOURCE_FLAG_NONE;
            const D3D12_RESOURCE_ALLOCATION_INFO rai = { size, DefaultAlignment::BUFFER_ALIGNMENT };
			const CD3DX12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(rai, flags);

            D3D12MA::Allocation* d3d12_alloc = memory->alloc;
            const D3D12_RESOURCE_STATES resource_state = get_d3d12_resource_read_state(type, memory->get_type() == MemoryType::Readback);
			ComPtr<ID3D12Resource> d3d12_buffer;
			CHECKHR(device6->CreatePlacedResource(d3d12_alloc->GetHeap(), d3d12_alloc->GetOffset(), &ResourceDesc, resource_state, nullptr, IID_PPV_ARGS(d3d12_buffer.GetAddressOf())));
            
            sg::SharedPtr<sg::Buffer> buffer(new sg::Buffer(type, size, uav_access, memory->get_type() == MemoryType::Upload, memory->get_type() == MemoryType::Readback));
            buffer->memory = memory;
            buffer->resource = d3d12_buffer;
            return buffer;
		}


		se::SharedPtr<sg::D3D12::Texture> Device::create_texture(SharedPtr<Memory> memory, u32 size, const ResourceCreateDesc& resource_desc)
		{
			D3D12_RESOURCE_DESC d3d12_desc = create_dx12_resource_desc(resource_desc);

			D3D12MA::Allocation* d3d12_alloc = memory->alloc;
			const D3D12_RESOURCE_STATES resource_state = get_d3d12_resource_read_state(BufferType::Texture);
			ComPtr<ID3D12Resource> d3d12_texture;
			CHECKHR(device6->CreatePlacedResource(d3d12_alloc->GetHeap(), d3d12_alloc->GetOffset(), &d3d12_desc, resource_state, nullptr, IID_PPV_ARGS(d3d12_texture.GetAddressOf())));

            bool uav_access = (resource_desc.usage_flags & ResourceUsageFlags::UnorderedAccess) == ResourceUsageFlags::UnorderedAccess;
			SharedPtr<Texture> buffer(new Texture(size, uav_access, memory->get_type() == MemoryType::Upload, memory->get_type() == MemoryType::Readback, resource_desc));
			buffer->memory = memory;
			buffer->resource = d3d12_texture;
			return buffer;
		}


        SharedPtr<RenderTargetView> Device::create_render_target_view(SharedPtr<Texture>& texture, DXGI_FORMAT format /*= DXGI_FORMAT_UNKNOWN*/)
		{
            seAssert(texture.get(), "Invalid texture pointer");

            SharedPtr<RenderTargetView> rtv(new RenderTargetView());
            rtv->texture_resource = texture;
            rtv->heap_index = rtv_descriptor_heap->allocate();
            rtv->rtv_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtv_descriptor_heap->get_cpu_handle_heap_start(), rtv->heap_index, rtv_descriptor_heap->get_increment_size());
            rtv->heap = rtv_descriptor_heap;

            D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
            D3D12_RENDER_TARGET_VIEW_DESC* rtv_desc_ptr = format == DXGI_FORMAT_UNKNOWN ? (D3D12_RENDER_TARGET_VIEW_DESC*)nullptr : &rtv_desc;
            if (rtv_desc_ptr)
            {
                rtv_desc_ptr->Format = format;
                seAssert(false, "Code path not texted and rtv_desc not filled out fully");
            }

			device->CreateRenderTargetView(texture->resource.Get(), rtv_desc_ptr, rtv->rtv_handle);
            return rtv;
		}

		void Device::free_render_target_view(RenderTargetView* rtv)
		{
            rtv_descriptor_heap->free(rtv->heap_index);
		}

		SharedPtr<DepthStencilView> Device::create_depth_stencil_view(SharedPtr<Texture>& texture, DXGI_FORMAT format /*= DXGI_FORMAT_UNKNOWN*/)
		{
			seAssert(texture.get(), "Invalid texture pointer");

			SharedPtr<DepthStencilView> dsv(new DepthStencilView());
            dsv->texture_resource = texture;
            dsv->heap_index = dsv_descriptor_heap->allocate();
			dsv->dsv_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(dsv_descriptor_heap->get_cpu_handle_heap_start(), dsv->heap_index, dsv_descriptor_heap->get_increment_size());
            dsv->heap = dsv_descriptor_heap;

			D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
            D3D12_DEPTH_STENCIL_VIEW_DESC* dsv_desc_ptr = format == DXGI_FORMAT_UNKNOWN ? (D3D12_DEPTH_STENCIL_VIEW_DESC*)nullptr : &dsv_desc;
			if (dsv_desc_ptr)
			{
                dsv_desc_ptr->Format = format;
				seAssert(false, "Code path not teted and rtv_desc not filled out fully");
			}

			device->CreateDepthStencilView(texture->resource.Get(), dsv_desc_ptr, dsv->dsv_handle);
			return dsv;

		}

		void Device::free_depth_stencil_view(DepthStencilView* dsv)
		{
			rtv_descriptor_heap->free(dsv->heap_index);
		}

		sg::ConstantBufferView Device::create_constant_buffer_view(Buffer* buffer, u64 offset, u64 size)
		{
			seAssert(offset % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT == 0, "Invalid offset");
            seAssert(size > 0, "Invalid size");
            seAssert(buffer != nullptr, "Invalid buffer");
			seAssert(buffer->get().Get() != nullptr, "Invalid buffer");
            seAssert(buffer->type == BufferType::Constant, "expecting constant buffer");

			ConstantBufferView cbv;
			D3D12_CONSTANT_BUFFER_VIEW_DESC& desc = cbv.desc;
			desc.BufferLocation = buffer->get()->GetGPUVirtualAddress() + offset;
			desc.SizeInBytes = (UINT)size;

			return cbv;
		}


		sg::D3D12::ShaderResourceView Device::create_shader_resource_view(SharedPtr<Buffer> buffer, u64 element_size, u64 element_count)
		{
            D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
			{
				desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				desc.Format = DXGI_FORMAT_UNKNOWN;
				desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

                desc.Buffer.FirstElement = 0;
				desc.Buffer.NumElements = (UINT) element_count;
				desc.Buffer.StructureByteStride = (UINT) element_size;
				desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
			}

			D3D12_SHADER_RESOURCE_VIEW_DESC desc_uint = {};
			{
				desc_uint.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				desc_uint.Format = DXGI_FORMAT_R32_TYPELESS;
				desc_uint.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				desc_uint.Buffer.NumElements = (UINT)element_count / 4;
				desc_uint.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
			}

            ShaderResourceView srv;
            srv.desc = desc;
            srv.desc_uint = desc_uint;
            srv.buffer_resource = buffer;
            return srv;
		}


		sg::D3D12::ShaderResourceView Device::create_shader_resource_view(SharedPtr<Texture> texture)
		{
			ShaderResourceView srv;
            srv.texture_resource = texture;
            srv.desc = {};
            srv.desc.Format = texture->resource_create_desc.format;
            if (srv.desc.Format == DXGI_FORMAT_D32_FLOAT)
            {
                srv.desc.Format = DXGI_FORMAT_R32_FLOAT;
            }
            srv.desc.ViewDimension = translate_srv(texture->resource_create_desc.dimension);
            srv.desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.desc.Texture2D.MipLevels = texture->resource_create_desc.mip_count;
            return srv;
		}

		sg::D3D12::UnorderedAccessView Device::create_unordered_access_view(SharedPtr<Buffer> buffer, u64 element_size, u64 element_count)
		{
            D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
			{
				desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				desc.Format = DXGI_FORMAT_UNKNOWN;
				desc.Buffer.CounterOffsetInBytes = 0;

				desc.Buffer.NumElements = (UINT) element_count;
				desc.Buffer.StructureByteStride = (UINT) element_size;
				desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
			}

            // Functions such as ClearUnorderedAccessViewUint cannot clear normal structured buffers so we must create different views when clearing them..
			D3D12_UNORDERED_ACCESS_VIEW_DESC desc_uint = {};
			{
				desc_uint.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				desc_uint.Format = DXGI_FORMAT_R32_TYPELESS;
				desc_uint.Buffer.CounterOffsetInBytes = 0;

				desc_uint.Buffer.NumElements = (UINT)(element_count * element_size) / 4; // DXGI_FORMAT_R32_TYPELESS is 4 bytes 
                desc_uint.Buffer.StructureByteStride = 0;// (UINT)element_size;
				desc_uint.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
			}

            UnorderedAccessView uav;
            uav.desc = desc;
            uav.desc_uint = desc_uint;
            uav.buffer_resource = buffer;
			return uav;
		}


		sg::UnorderedAccessView Device::create_unordered_access_view(SharedPtr<Texture> texture)
		{
            seAssert(texture->resource_create_desc.dimension == ResourceDimension::Texture2D, "Only supported this path currently");
			D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
			{
				desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
				desc.Format = texture->resource_create_desc.format;
			}

			UnorderedAccessView uav;
			uav.desc = desc;
            uav.desc_uint = {};
			uav.texture_resource = texture;
			return uav;
		}

		VertexBufferView Device::create_vertex_buffer_view(SharedPtr<Buffer> buffer, u64 offset, u64 size, u64 stride)
		{
            VertexBufferView vbv;
            vbv.buffer_resource = buffer;

            D3D12_VERTEX_BUFFER_VIEW& d3d12_vbv = vbv.vbv;
            d3d12_vbv.BufferLocation = buffer->get()->GetGPUVirtualAddress() + offset;
            d3d12_vbv.SizeInBytes = size;
            d3d12_vbv.StrideInBytes = stride;

            return vbv;
		}


		IndexBufferView Device::create_index_buffer_view(SharedPtr<Buffer> buffer, u64 offset, u64 size, DXGI_FORMAT format)
		{
            IndexBufferView ibv;
			ibv.buffer_resource = buffer;

			D3D12_INDEX_BUFFER_VIEW& d3d12_ibv = ibv.ibv;
            d3d12_ibv.BufferLocation = buffer->get()->GetGPUVirtualAddress() + offset;
            d3d12_ibv.SizeInBytes = size;
            d3d12_ibv.Format = format;
            
            return ibv;
		}


		bool Device::SupportsMeshShaders() { return features.MeshShaderTier() >= D3D12_MESH_SHADER_TIER_1; }


		bool Device::SupportsWaveOps()
		{
            return features.WaveOps();
		}

		sg::u32 Device::GetWaveLaneCountMin()
		{
            return features.WaveLaneCountMin();
		}

		sg::u32 Device::GetWaveLaneCountMax()
		{
            return features.WaveLaneCountMax();
		}

		sg::u32 Device::GetTotalLaneCount()
		{
            return features.TotalLaneCount();
		}

        u32 Device::GetFormatBitsPerUnit(DXGI_FORMAT format)
        {
			return D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(format);
        }

        u32 Device::GetFormatWidthAlignment(DXGI_FORMAT format)
        {
			return D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetWidthAlignment(format);
        }

        LPCSTR Device::GetFormatName(DXGI_FORMAT format)
        {
			return D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetName(format);
        }


		sg::u64 Device::CalculateResourceSize(u32 width, u32 height, u32 depth, u32 mip_levels, DXGI_FORMAT format)
		{
            SIZE_T size;
            if (D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::CalculateResourceSize(width, height, depth, format, mip_levels, 1, size) == S_OK)
            {
                return size;
            }
            return 0;
		}

		u32 Device::create_swap_chain(HWND hwnd, CommandQueue* command_queue, u32 buffer_count, DXGI_FORMAT format, u32 width, u32 height, SharedPtr<RenderTargetView>* rtv_list)
        {
            swap_chain_buffer_count = buffer_count;

            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.BufferCount = swap_chain_buffer_count;
            desc.Width = width;
            desc.Height = height;
            desc.Format = format;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.SampleDesc.Count = 1;
            HRESULT hr = factory->CreateSwapChainForHwnd(command_queue->get().Get(), hwnd, &desc, nullptr, nullptr, swap_chain.GetAddressOf());
            CHECKHR(hr);
            CHECKHR(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

            for (u32 i = 0; i < swap_chain_buffer_count; i++)
            {
                SharedPtr<Texture> texture_resource = SharedPtr<Texture>(new Texture(false, {}, true));
                CHECKHR(swap_chain->GetBuffer(i, IID_PPV_ARGS(texture_resource->resource.GetAddressOf())))
                rtv_list[i] = create_render_target_view(texture_resource);
            }

            return SUCCEEDED(hr) ? 0 : ~0;
        }

        u32 Device::present_swap_chain(CommandQueue* command_queue)
        {
            CHECKHR(swap_chain->Present(1, 0));

            swap_chain_current_index++;
            swap_chain_current_index = swap_chain_current_index % swap_chain_buffer_count;
            return swap_chain_current_index;
        }

        ComPtr<ID3D12DescriptorHeap> Device::get_rtv_descriptor_heap()
        {
            return rtv_descriptor_heap->get_heap();
        }


		ComPtr<ID3D12DescriptorHeap> Device::get_sampler_descriptor_heap()
		{
            return sampler_descriptor_heap->get_heap();
		}

		u32 Device::get_rtv_descriptor_heap_increment_size()
        {
            return rtv_descriptor_heap->get_increment_size();
        }


		ComPtr<ID3D12RootSignature> Device::create_root_signature(const BindingDesc& binding_desc, bool compute)
		{
            //https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-version-1-1#static-and-volatile-flags
            CD3DX12_DESCRIPTOR_RANGE1 ranges[4];
			ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, binding_desc.cbv_binding_count, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
			ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, binding_desc.srv_binding_count, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
			ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, binding_desc.uav_binding_count, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
			ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, binding_desc.sampler_binding_count, 0);

			CD3DX12_ROOT_PARAMETER1 rootParameters[4];
			u32 parameter_index = 0;
			if (binding_desc.cbv_binding_count)
			{
				rootParameters[parameter_index].InitAsDescriptorTable(binding_desc.cbv_binding_count > 0 ? 1 : 0, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
				parameter_index++;
			}
			if (binding_desc.srv_binding_count)
			{
				rootParameters[parameter_index].InitAsDescriptorTable(binding_desc.srv_binding_count > 0 ? 1 : 0, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
				parameter_index++;
			}
			if (binding_desc.uav_binding_count)
			{
				rootParameters[parameter_index].InitAsDescriptorTable(binding_desc.uav_binding_count > 0 ? 1 : 0, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);
				parameter_index++;
			}
			if (binding_desc.sampler_binding_count)
			{
				rootParameters[parameter_index].InitAsDescriptorTable(binding_desc.sampler_binding_count > 0 ? 1 : 0, &ranges[3], D3D12_SHADER_VISIBILITY_ALL);
				parameter_index++;
			}

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init_1_1(parameter_index, rootParameters, 0, nullptr, compute ? D3D12_ROOT_SIGNATURE_FLAG_NONE : D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			ComPtr<ID3DBlob> signature;
			ComPtr<ID3DBlob> error;
			CHECKHR(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, features.HighestRootSignatureVersion(), &signature, &error));
			if (error)
			{
				seWriteLine("D3DX12SerializeVersionedRootSignature Error: %s", error->GetBufferPointer());
			}
            ComPtr<ID3D12RootSignature> rs;
			CHECKHR(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(rs.GetAddressOf())));
            return rs;
        }

		void Device::create_descriptors()
        {
            //cbv srv uav
            {
				D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
				cbvSrvHeapDesc.NumDescriptors = DESCRIPTOR_COUNT;
				cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

				{
					cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
					ComPtr<ID3D12DescriptorHeap> heap;
					CHECKHR(device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&heap)));
					heap->SetName(L"Imgui CBV SRV UAV Heap");
					u32 increment_size = device->GetDescriptorHandleIncrementSize(cbvSrvHeapDesc.Type);
                    cbv_srv_uav_descriptor_heap_imgui = SharedPtr<DescriptorHeap>(new DescriptorHeap(heap, cbvSrvHeapDesc.NumDescriptors, increment_size));
				}
            }
            //rtv
            {
                ComPtr<ID3D12DescriptorHeap> heap;
                D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
                rtvDesc.NumDescriptors = DESCRIPTOR_COUNT;
                rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                CHECKHR(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&heap)));
                heap->SetName(L"Device RTV Heap");
                u32 increment_size = device->GetDescriptorHandleIncrementSize(rtvDesc.Type);
                rtv_descriptor_heap = SharedPtr<DescriptorHeap>(new DescriptorHeap(heap, rtvDesc.NumDescriptors, increment_size));
            }
            //dsv
            {
                ComPtr<ID3D12DescriptorHeap> heap;
                D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
                dsvDesc.NumDescriptors = DESCRIPTOR_COUNT;
                dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
                dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                CHECKHR(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&heap)));
                heap->SetName(L"Device DSV Heap");
                u32 increment_size = device->GetDescriptorHandleIncrementSize(dsvDesc.Type);
                dsv_descriptor_heap = SharedPtr<DescriptorHeap>(new DescriptorHeap(heap, dsvDesc.NumDescriptors, increment_size));
            }
            //sampler
            {
                ComPtr<ID3D12DescriptorHeap> heap;
                D3D12_DESCRIPTOR_HEAP_DESC splrDesc = {};
                splrDesc.NumDescriptors = DESCRIPTOR_COUNT;
                splrDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
                splrDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                CHECKHR(device->CreateDescriptorHeap(&splrDesc, IID_PPV_ARGS(&heap)));
                heap->SetName(L"Device Sampler Heap");
                u32 increment_size = device->GetDescriptorHandleIncrementSize(splrDesc.Type);
                sampler_descriptor_heap = SharedPtr<DescriptorHeap>(new DescriptorHeap(heap, splrDesc.NumDescriptors, increment_size));
            }
        }
	}
}