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

//D3D12 Memory Allocator
#include <D3D12MemAlloc.h>

namespace sg
{
	namespace D3D12
	{
        //https://learn.microsoft.com/en-us/cpp/cpp/pimpl-for-compile-time-encapsulation-modern-cpp?view=msvc-170
		PoolPIMPL::PoolPIMPL() = default;
		PoolPIMPL::~PoolPIMPL() = default;

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
				ComPtr<IDXGIAdapter4> hardware_adapter_4;
				hardware_adapter->QueryInterface(IID_PPV_ARGS(&hardware_adapter_4));

				DXGI_ADAPTER_DESC3 desc3;
				CHECKHR(hardware_adapter_4->GetDesc3(&desc3));
				seWriteLine("%ls", desc3.Description);

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
                DXGI_ADAPTER_DESC1 adapter_desc;
                CHECKHR(hardware_adapter->GetDesc1(&adapter_desc));
                seAssert(adapter_desc.DedicatedVideoMemory > 0, "Expecting DedicatedVideoMemory");
                const u64 total_memory_to_use = (adapter_desc.DedicatedVideoMemory * ADAPTER_MEMORY_TO_CONSUME_PERCENTAGE) / 100;

                //mempool_textures
                {
                    D3D12_HEAP_DESC desc = {};
                    desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
                    desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                    desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
                    desc.SizeInBytes = ((total_memory_to_use * TEXTURE_POOL_PERCENTAGE) / 100);
                    device->CreateHeap(&desc, IID_PPV_ARGS(mempool_textures.heap.GetAddressOf()));

                    D3D12MA::VIRTUAL_BLOCK_DESC vbd = {};
                    vbd.Size = desc.SizeInBytes;
                    CHECKHR(D3D12MA::CreateVirtualBlock(&vbd, mempool_textures.virtual_block.GetAddressOf()));
                    mempool_textures.heap->SetName(L"Texture Heap");
                }

                //mempool_targets
                {
					D3D12_HEAP_DESC desc = {};
					desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
					desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
					desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
					desc.SizeInBytes = ((total_memory_to_use * TARGET_POOL_PERCENTAGE) / 100);
					device->CreateHeap(&desc, IID_PPV_ARGS(mempool_targets.heap.GetAddressOf()));

					D3D12MA::VIRTUAL_BLOCK_DESC vbd = {};
					vbd.Size = desc.SizeInBytes;
					CHECKHR(D3D12MA::CreateVirtualBlock(&vbd, mempool_targets.virtual_block.GetAddressOf()));
                    mempool_targets.heap->SetName(L"Render & Depth Target Heap");
                }

                //mempool_buffers
                {
					D3D12_HEAP_DESC desc = {};
					desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
					desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
					desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
					desc.SizeInBytes = ((total_memory_to_use * BUFFER_POOL_PERCENTAGE) / 100);
					device->CreateHeap(&desc, IID_PPV_ARGS(mempool_buffers.heap.GetAddressOf()));

					D3D12MA::VIRTUAL_BLOCK_DESC vbd = {};
					vbd.Size = desc.SizeInBytes;
					CHECKHR(D3D12MA::CreateVirtualBlock(&vbd, mempool_buffers.virtual_block.GetAddressOf()));
                    mempool_buffers.heap->SetName(L"Buffer Heap");
                }
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

            if (!ImGui_ImplDX12_Init(&init_info))
            {
                seAssert(false, "Error: ImGui_ImplDX12_Init failed!" );
            }
        }

        void Device::imgui_render(CommandList* command_list)
        {
            ID3D12GraphicsCommandList6* d3d_cmd_list = command_list->get().Get();

            ID3D12DescriptorHeap* heap = cbv_srv_uav_descriptor_heap_imgui->get_heap().Get();
            d3d_cmd_list->SetDescriptorHeaps(1, &heap);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3d_cmd_list);
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
            if (type == MemoryType::Upload)
            {
				PoolPIMPL pool;
				D3D12_HEAP_DESC desc = {};
				desc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
				desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
				desc.Flags = D3D12_HEAP_FLAG_NONE | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
				desc.SizeInBytes = size;
				device->CreateHeap(&desc, IID_PPV_ARGS(pool.heap.GetAddressOf()));
				pool->SetName(L"Upload Heap");

				D3D12MA::VIRTUAL_BLOCK_DESC vbd = {};
				vbd.Size = desc.SizeInBytes;
				CHECKHR(D3D12MA::CreateVirtualBlock(&vbd, pool.virtual_block.GetAddressOf()));

				Allocation out_alloc;
				D3D12MA::VIRTUAL_ALLOCATION_DESC vad = { D3D12MA::VIRTUAL_ALLOCATION_FLAG_NONE, size, alignment };
				CHECKHR(pool.virtual_block->Allocate(&vad, out_alloc.virtual_allocation_cast(), &out_alloc.offset));
				out_alloc.virtual_block = pool.virtual_block;
				out_alloc.heap = pool.heap;

				return SharedPtr<Memory>(new Memory(type, out_alloc));
            }
            else if (type == MemoryType::Readback)
			{
				PoolPIMPL pool;
				D3D12_HEAP_DESC desc = {};
				desc.Properties.Type = D3D12_HEAP_TYPE_READBACK;
				desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
				desc.Flags = D3D12_HEAP_FLAG_NONE | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
				desc.SizeInBytes = size;
				device->CreateHeap(&desc, IID_PPV_ARGS(pool.heap.GetAddressOf()));
				pool->SetName(L"Upload Heap");

				D3D12MA::VIRTUAL_BLOCK_DESC vbd = {};
				vbd.Size = desc.SizeInBytes;
				CHECKHR(D3D12MA::CreateVirtualBlock(&vbd, pool.virtual_block.GetAddressOf()));

				Allocation out_alloc;
				D3D12MA::VIRTUAL_ALLOCATION_DESC vad = { D3D12MA::VIRTUAL_ALLOCATION_FLAG_NONE, size, alignment };
				CHECKHR(pool.virtual_block->Allocate(&vad, out_alloc.virtual_allocation_cast(), &out_alloc.offset));
				out_alloc.virtual_block = pool.virtual_block;
				out_alloc.heap = pool.heap;

				return SharedPtr<Memory>(new Memory(type, out_alloc));
            }
            else //if (type == MemoryType::GPUOptimal)
            {
                PoolPIMPL* pool = nullptr;
                switch (sub_type)
                {
                case MemorySubType::Texture: pool = &mempool_textures; break;
                case MemorySubType::Target: pool = &mempool_targets; break;
                case MemorySubType::Buffer: pool = &mempool_buffers; break;
                default:
                    seAssert(false, "Missing Pool Type");
                }

                Allocation out_alloc;
                D3D12MA::VIRTUAL_ALLOCATION_DESC vad = { D3D12MA::VIRTUAL_ALLOCATION_FLAG_NONE, size, alignment };
                CHECKHR(pool->virtual_block->Allocate(&vad, out_alloc.virtual_allocation_cast(), &out_alloc.offset));
                out_alloc.virtual_block = pool->virtual_block;
                out_alloc.heap = pool->heap;

				return SharedPtr<Memory>(new Memory(type, out_alloc));
            }
        }

        ComPtr<QueueFence> Device::create_queue_fence()
        {
            ComPtr<QueueFence> fence;
            CHECKHR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())))
            return fence;
        }


		Ptr<GPUTimestampPool> Device::create_gpu_timestamp_pool(CommandQueue* queue, u32 max_timestamps)
		{
            ID3D12CommandQueue* d3d12_queue = queue->get().Get();
            return Ptr<GPUTimestampPool>(new GPUTimestampPool(device.Get(), d3d12_queue, max_timestamps));
		}


		se::Ptr<sg::D3D12::GPUStatisticPool> Device::create_gpu_statistic_pool(CommandQueue* queue, u32 max_stats)
		{
			ID3D12CommandQueue* d3d12_queue = queue->get().Get();
			return Ptr<GPUStatisticPool>(new GPUStatisticPool(device6.Get(), d3d12_queue, max_stats));
		}

		Ptr<CommandQueue> Device::create_command_queue()
        {
            ComPtr<ID3D12CommandQueue> queue;
            D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
            QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            CHECKHR(device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&queue)));
            CHECKHR(queue->SetName(L"CommandQueue"));
            return Ptr<CommandQueue>(new CommandQueue(queue));
        }

        Ptr<CommandList> Device::create_command_buffer()
        {
            ComPtr<ID3D12GraphicsCommandList6> command_list;
            ComPtr<ID3D12CommandAllocator> command_allocator;

            CHECKHR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator)));
            CHECKHR(device6->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator.Get(), nullptr, IID_PPV_ARGS(&command_list)));
            CHECKHR(command_list->Close());

            Ptr<CommandList> out_command_list = Ptr<CommandList>(new CommandList(command_list, command_allocator));
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
            }

            out_command_list->device = device;

            out_command_list->global_cbv_srv_uav_descriptor_heap = cbv_srv_uav_descriptor_heap;
            out_command_list->global_rtv_descriptor_heap = rtv_descriptor_heap->get_heap();
            out_command_list->global_dsv_descriptor_heap = dsv_descriptor_heap->get_heap();
            out_command_list->global_sampler_descriptor_heap = sampler_descriptor_heap->get_heap();
            
            out_command_list->descriptor_increment_size_cbv_srv_uav = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            out_command_list->descriptor_increment_size_rtv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            out_command_list->descriptor_increment_size_dsv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            out_command_list->descriptor_increment_size_sampler = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

            return out_command_list;
        }

        Ptr<VertexShader> Device::create_vertex_shader(const std::vector<uint8_t>& shader)
        {
            return Ptr<VertexShader>(new VertexShader(shader));
        }

        Ptr<PixelShader> Device::create_pixel_shader(const std::vector<uint8_t>& shader)
        {
            return Ptr<PixelShader>(new PixelShader(shader));
        }

		sg::Ptr<sg::ComputeShader> Device::create_compute_shader(const std::vector<uint8_t>& shader)
		{
			return Ptr<ComputeShader>(new ComputeShader(shader));
		}


		sg::Ptr<sg::MeshShader> Device::create_mesh_shader(const std::vector<uint8_t>& shader)
		{
			return Ptr<MeshShader>(new MeshShader(shader));
		}

		Ptr<Pipeline> Device::create_pipeline(const PipelineDesc::Graphics& pipeline_desc, const BindingDesc& binding_desc)
        {
            Ptr<Pipeline> out_pipeline = Ptr<Pipeline>(new Pipeline(false));

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


		se::Ptr<sg::Pipeline> Device::create_pipeline(const PipelineDesc::Compute& pipeline_desc, const BindingDesc& binding_desc)
		{
			Ptr<Pipeline> out_pipeline = Ptr<Pipeline>(new Pipeline(true));

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

		sg::Ptr<sg::Pipeline> Device::create_pipeline(const PipelineDesc::Mesh& pipeline_desc, const BindingDesc& binding_desc)
		{
            if (!SupportsMeshShaders())
            {
                return nullptr;
            }

			Ptr<Pipeline> out_pipeline = Ptr<Pipeline>(new Pipeline(false));

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

		sg::SharedPtr<sg::Buffer> Device::create_buffer(SharedPtr<Memory> memory, u32 size, u32 alignment, BufferType type, bool uav_access)
		{
            const D3D12_RESOURCE_FLAGS flags = uav_access ? (D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) : D3D12_RESOURCE_FLAG_NONE;
            const D3D12_RESOURCE_ALLOCATION_INFO rai = { size, alignment };
			const CD3DX12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(rai, flags);

            Allocation& d3d12_alloc = memory->alloc;
            const D3D12_RESOURCE_STATES resource_state = get_d3d12_resource_read_state(type);
			ComPtr<ID3D12Resource> d3d12_buffer;
			CHECKHR(device6->CreatePlacedResource(d3d12_alloc.heap.Get(), d3d12_alloc.offset, &ResourceDesc, resource_state, nullptr, IID_PPV_ARGS(d3d12_buffer.GetAddressOf())));
            
            sg::SharedPtr<sg::Buffer> buffer(new sg::Buffer(type, size, uav_access, memory->get_type() == MemoryType::Upload, memory->get_type() == MemoryType::Readback));
            buffer->memory = memory;
            buffer->resource = d3d12_buffer;
            return buffer;
		}


		se::SharedPtr<sg::D3D12::Texture> Device::create_texture(SharedPtr<Memory> memory, u32 size, u32 alignment, const ResourceCreateDesc& resource_desc)
		{
			D3D12_RESOURCE_DESC d3d12_desc = create_dx12_resource_desc(resource_desc);

			Allocation& d3d12_alloc = memory->alloc;
			const D3D12_RESOURCE_STATES resource_state = get_d3d12_resource_read_state(BufferType::Texture);
			ComPtr<ID3D12Resource> d3d12_texture;
			CHECKHR(device6->CreatePlacedResource(d3d12_alloc.heap.Get(), d3d12_alloc.offset, &d3d12_desc, resource_state, nullptr, IID_PPV_ARGS(d3d12_texture.GetAddressOf())));

            bool uav_access = (resource_desc.usage_flags & ResourceUsageFlags::UnorderedAccess) == ResourceUsageFlags::UnorderedAccess;
			SharedPtr<Texture> buffer(new Texture(size, uav_access, memory->get_type() == MemoryType::Upload, memory->get_type() == MemoryType::Readback));
			buffer->memory = memory;
			buffer->resource = d3d12_texture;
			return buffer;
		}


        RenderTargetView Device::create_render_target_view(SharedPtr<Texture>& texture, DXGI_FORMAT format /*= DXGI_FORMAT_UNKNOWN*/)
		{
            seAssert(texture.get(), "Invalid texture pointer");

            RenderTargetView rtv;
            rtv.texture_resource = texture;
            rtv.rtv = rtv_descriptor_heap->allocate();
			CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_descriptor_heap->get_cpu_handle_heap_start(), rtv.rtv, rtv_descriptor_heap->get_increment_size());

            D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
            D3D12_RENDER_TARGET_VIEW_DESC* rtv_desc_ptr = format == DXGI_FORMAT_UNKNOWN ? (D3D12_RENDER_TARGET_VIEW_DESC*)nullptr : &rtv_desc;
            if (rtv_desc_ptr)
            {
                rtv_desc_ptr->Format = format;
                seAssert(false, "Code path not texted and rtv_desc not filled out fully");
            }

			device->CreateRenderTargetView(texture->resource.Get(), rtv_desc_ptr, rtv_handle);
            return rtv;
		}


        DepthStencilView Device::create_depth_stencil_view(SharedPtr<Texture>& texture, DXGI_FORMAT format /*= DXGI_FORMAT_UNKNOWN*/)
		{
			seAssert(texture.get(), "Invalid texture pointer");

			DepthStencilView dsv;
            dsv.texture_resource = texture;
            dsv.dsv = dsv_descriptor_heap->allocate();
			CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(dsv_descriptor_heap->get_cpu_handle_heap_start(), dsv.dsv, dsv_descriptor_heap->get_increment_size());

			D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
            D3D12_DEPTH_STENCIL_VIEW_DESC* dsv_desc_ptr = format == DXGI_FORMAT_UNKNOWN ? (D3D12_DEPTH_STENCIL_VIEW_DESC*)nullptr : &dsv_desc;
			if (dsv_desc_ptr)
			{
                dsv_desc_ptr->Format = format;
				seAssert(false, "Code path not texted and rtv_desc not filled out fully");
			}

			device->CreateDepthStencilView(texture->resource.Get(), dsv_desc_ptr, dsv_handle);
			return dsv;

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

			//u32 idx = cbv_srv_uav_descriptor_heap->allocate();
			//CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_handle(cbv_srv_uav_descriptor_heap->get_cpu_handle_heap_start(), idx, //cbv_srv_uav_descriptor_heap->get_increment_size());
            //
			//device->CreateShaderResourceView(buffer->get().Get(), &desc, cpu_handle);

            ShaderResourceView srv;
            srv.desc = desc;
            srv.desc_uint = desc_uint;
            srv.buffer_resource = buffer;
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

			//u32 idx = cbv_srv_uav_descriptor_heap->allocate();
			//CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_handle(cbv_srv_uav_descriptor_heap->get_cpu_handle_heap_start(), idx, //cbv_srv_uav_descriptor_heap->get_increment_size());
            //
			//device->CreateUnorderedAccessView(buffer->get().Get(),nullptr, &desc, cpu_handle);

            UnorderedAccessView uav;
            uav.desc = desc;
            uav.desc_uint = desc_uint;
            uav.buffer_resource = buffer;
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

		u32 Device::create_swap_chain(HWND hwnd, CommandQueue* command_queue, u32 buffer_count, DXGI_FORMAT format, u32 width, u32 height, RenderTargetView* rtv_list)
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
                SharedPtr<Texture> texture_resource = SharedPtr<Texture>(new Texture(false));
                rtv_list[i].texture_resource = texture_resource;
                CHECKHR(swap_chain->GetBuffer(i, IID_PPV_ARGS(texture_resource->resource.GetAddressOf())));

                rtv_list[i].rtv = rtv_descriptor_heap->allocate();
                CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_descriptor_heap->get_cpu_handle_heap_start(), rtv_list[i].rtv, rtv_descriptor_heap->get_increment_size());
                device->CreateRenderTargetView(texture_resource->resource.Get(), nullptr, rtv_handle);
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

        ComPtr<ID3D12DescriptorHeap> Device::get_cbv_srv_uav_descriptor_heap()
        {
            return cbv_srv_uav_descriptor_heap->get_heap();
        }

        ComPtr<ID3D12DescriptorHeap> Device::get_rtv_descriptor_heap()
        {
            return rtv_descriptor_heap->get_heap();
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
					ComPtr<ID3D12DescriptorHeap> heap;
					CHECKHR(device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&heap)));
					heap->SetName(L"Device CBV SRV UAV Heap");
					u32 increment_size = device->GetDescriptorHandleIncrementSize(cbvSrvHeapDesc.Type);
					cbv_srv_uav_descriptor_heap = SharedPtr<DescriptorHeap>(new DescriptorHeap(heap, cbvSrvHeapDesc.NumDescriptors, increment_size));
				}
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