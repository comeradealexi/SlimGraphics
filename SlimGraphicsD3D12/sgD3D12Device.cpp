#include "pch.h"
#include "sgD3D12Device.h"
#include "sgD3D12CommandQueue.h"
#include "sgD3D12CommandList.h"
#include "sgD3D12RenderTargetView.h"
#include "sgD3D12ConstantBufferView.h"
#include "sgD3D12Pipeline.h"
#include "sgD3D12TypesTranslator.h"
#include "sgD3D12GPUTimestampPool.h"
#include <imgui_impl_dx12.h>

//D3D12 Memory Allocator
#include <D3D12MemAlloc.h>

namespace sg
{
	namespace D3D12
	{
        //https://learn.microsoft.com/en-us/cpp/cpp/pimpl-for-compile-time-encapsulation-modern-cpp?view=msvc-170
        AllocatorPIMPL::AllocatorPIMPL() = default;
        AllocatorPIMPL::~AllocatorPIMPL() = default;
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
#if _DEBUG
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
                    }
                }
            #endif

            CHECKHR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

            ComPtr<IDXGIAdapter1> hardware_adapter;
            GetHardwareAdapter(factory.Get(), &hardware_adapter, true, feature_level);

            CHECKHR(D3D12CreateDevice(hardware_adapter.Get(), feature_level, IID_PPV_ARGS(&device)));
            CHECKHR(device->QueryInterface(IID_PPV_ARGS(&device6)));
            seWriteLine("D3D12 Device created.");

            setup_debug_filters(device.Get());

            features.Init(device.Get());
           
            create_descriptors();

            //Create memory allocators
            {
                DXGI_ADAPTER_DESC1 adapter_desc;
                CHECKHR(hardware_adapter->GetDesc1(&adapter_desc));
                seAssert(adapter_desc.DedicatedVideoMemory > 0, "Expecting DedicatedVideoMemory");
                const u64 total_memory_to_use = (adapter_desc.DedicatedVideoMemory * ADAPTER_MEMORY_TO_CONSUME_PERCENTAGE) / 100;

                D3D12MA::ALLOCATOR_DESC desc = {};
                desc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED | D3D12MA::ALLOCATOR_FLAG_SINGLETHREADED;
                desc.pDevice = device.Get();
                desc.pAdapter = hardware_adapter.Get();
                CHECKHR(D3D12MA::CreateAllocator(&desc, &allocator.ptr));

                //mempool_textures
                {
                    D3D12MA::POOL_DESC desc = {};
                    desc.HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
                    desc.HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                    desc.HeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
                    desc.BlockSize = ((total_memory_to_use * TEXTURE_POOL_PERCENTAGE) / 100);
                    desc.MinBlockCount = 1;
                    desc.MaxBlockCount = 1;
                    desc.MinAllocationAlignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
                    CHECKHR(allocator.ptr->CreatePool(&desc, mempool_textures.ptr.GetAddressOf()));
                    mempool_textures->SetName(L"Texture Heap");
                }

                //mempool_targets
                {
                    D3D12MA::POOL_DESC desc = {};
                    desc.HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
                    desc.HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                    desc.HeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
                    desc.BlockSize = ((total_memory_to_use * TARGET_POOL_PERCENTAGE) / 100);
                    desc.MinBlockCount = 1;
                    desc.MaxBlockCount = 1;
                    desc.MinAllocationAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
                    CHECKHR(allocator.ptr->CreatePool(&desc, mempool_targets.ptr.GetAddressOf()));
                    mempool_targets->SetName(L"Render & Depth Target Heap");
                }

                //mempool_buffers
                {
                    D3D12MA::POOL_DESC desc = {};
                    desc.HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
                    desc.HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                    desc.HeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
                    desc.BlockSize = ((total_memory_to_use * BUFFER_POOL_PERCENTAGE) / 100);
                    desc.MinBlockCount = 1;
                    desc.MaxBlockCount = 1;
                    desc.MinAllocationAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
                    CHECKHR(allocator.ptr->CreatePool(&desc, mempool_buffers.ptr.GetAddressOf()));
                    mempool_buffers->SetName(L"Buffer Heap");
                }
                seWriteLine("Created all allocators.");
            }
		}

        void Device::imgui_init(u32 num_frames, DXGI_FORMAT format)
        {
            u32 idx = imgui_cbv_srv_uav_descriptor_heap->allocate();
            CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_handle(imgui_cbv_srv_uav_descriptor_heap->get_cpu_handle_heap_start(), idx, imgui_cbv_srv_uav_descriptor_heap->get_increment_size());
            CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_handle(imgui_cbv_srv_uav_descriptor_heap->get_gpu_handle_heap_start(), idx, imgui_cbv_srv_uav_descriptor_heap->get_increment_size());
            if (!ImGui_ImplDX12_Init(device.Get(), num_frames, format, imgui_cbv_srv_uav_descriptor_heap->get_heap().Get(), cpu_handle, gpu_handle))
            {
                seAssert(false, "Error: ImGui_ImplDX12_Init failed!" );
            }
        }

        void Device::imgui_render(CommandList* command_list)
        {
            ID3D12GraphicsCommandList6* d3d_cmd_list = command_list->get().Get();

            ID3D12DescriptorHeap* heap = imgui_cbv_srv_uav_descriptor_heap->get_heap().Get();
            d3d_cmd_list->SetDescriptorHeaps(1, &heap);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3d_cmd_list);
        }

        SharedPtr<Memory> Device::allocate_memory(MemoryType type, MemorySubType sub_type, u64 size, u64 alignment)
        {
            D3D12MA::Pool* pool = nullptr;
            if (type == MemoryType::GPUOptimal)
            {
                switch (sub_type)
                {
                case MemorySubType::Texture: pool = mempool_textures.ptr.Get(); break;
                case MemorySubType::Target: pool = mempool_targets.ptr.Get(); break;
                case MemorySubType::Buffer: pool = mempool_buffers.ptr.Get(); break;
                default:
                    seAssert(false, "Missing Pool Type")
                }
            }

            D3D12MA::ALLOCATION_DESC ad = {};
            ad.CustomPool = pool;

            D3D12_RESOURCE_ALLOCATION_INFO alloc_info{ size, alignment };

            ComPtr<D3D12MA::Allocation> out_alloc;
            CHECKHR(allocator->AllocateMemory(&ad, &alloc_info, out_alloc.GetAddressOf()));

            return SharedPtr<Memory>(new Memory(out_alloc.Get()));
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
                heap->SetName(L"CBV SRV UAV Heap");
                u32 increment_size = device->GetDescriptorHandleIncrementSize(cbvSrvHeapDesc.Type);
                out_command_list->descriptor_heap_increment = increment_size;
                out_command_list->descriptor_heap_maximum = cbvSrvHeapDesc.NumDescriptors;
                out_command_list->descriptor_heap = heap;
            }

            out_command_list->device = device;

            out_command_list->global_cbv_srv_uav_descriptor_heap = cbv_srv_uav_descriptor_heap->get_heap();
            out_command_list->global_rtv_descriptor_heap = rtv_descriptor_heap->get_heap();
            out_command_list->global_dsv_descriptor_heap = dsv_descriptor_heap->get_heap();
            out_command_list->global_sampler_descriptor_heap = sampler_descriptor_heap->get_heap();
            
            out_command_list->descriptor_increment_size_cbv_srv_uav = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            out_command_list->descriptor_increment_size_rtv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            out_command_list->descriptor_increment_size_dsv = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            out_command_list->descriptor_increment_size_sampler = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

            return out_command_list;
        }

        Ptr<VertexShader> Device::create_vertex_shader(std::vector<uint8_t>& shader)
        {
            return Ptr<VertexShader>(new VertexShader(shader));
        }

        Ptr<PixelShader> Device::create_pixel_shader(std::vector<uint8_t>& shader)
        {
            return Ptr<PixelShader>(new PixelShader(shader));
        }

        Ptr<Pipeline> Device::create_pipeline(const PipelineDesc::Graphics& pipeline_desc, const BindingDesc& binding_desc)
        {
            Ptr<Pipeline> out_pipeline = Ptr<Pipeline>(new Pipeline());

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
            
            //Root Signature Generation
            {
                CD3DX12_DESCRIPTOR_RANGE1 ranges[4];
                ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, binding_desc.cbv_binding_count, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);    // Diffuse texture + array of materials.
                ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, binding_desc.srv_binding_count, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
                ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, binding_desc.uav_binding_count, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
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
                    rootParameters[parameter_index].InitAsDescriptorTable(binding_desc.srv_binding_count > 0 ? 1 : 0, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
                    parameter_index++;
                }
                if (binding_desc.uav_binding_count)
                {
                    rootParameters[parameter_index].InitAsDescriptorTable(binding_desc.uav_binding_count > 0 ? 1 : 0, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
                    parameter_index++;
                }
                if (binding_desc.sampler_binding_count)
                {
                    rootParameters[parameter_index].InitAsDescriptorTable(binding_desc.sampler_binding_count > 0 ? 1 : 0, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
                    parameter_index++;
                }

                CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
                rootSignatureDesc.Init_1_1(parameter_index, rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
                
                ComPtr<ID3DBlob> signature;
                ComPtr<ID3DBlob> error;
                CHECKHR(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, features.HighestRootSignatureVersion(), &signature, &error));
                if (error)
                {
                    seWriteLine("D3DX12SerializeVersionedRootSignature Error: %s", error->GetBufferPointer());
                }
                CHECKHR(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(out_pipeline->root_signature.GetAddressOf())));
                psoDesc.pRootSignature = out_pipeline->root_signature.Get();
            }

            CHECKHR(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(out_pipeline->pipeline.GetAddressOf())));
            return out_pipeline;
        }


		sg::Ptr<sg::Buffer> Device::create_buffer(SharedPtr<Memory> memory, u32 size, u32 alignment, bool unordered_access /*= false*/)
		{
            const D3D12_RESOURCE_FLAGS flags = unordered_access ? D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE : D3D12_RESOURCE_FLAG_NONE;
            const D3D12_RESOURCE_ALLOCATION_INFO rai = { size, alignment };
            const CD3DX12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(rai, flags);

            D3D12MA::Allocation* alloc = memory->memory_ptr.ptr.Get();
            
			ComPtr<ID3D12Resource> d3d12_buffer;
            CHECKHR(device6->CreatePlacedResource(alloc->GetHeap(), alloc->GetOffset(), &ResourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(d3d12_buffer.GetAddressOf())));

            sg::Ptr<sg::Buffer> buffer(new sg::Buffer());
            buffer->memory = memory;
            buffer->resource = d3d12_buffer;
            return buffer;
		}


		sg::ConstantBufferView Device::create_constant_buffer_view(Buffer* buffer, u64 offset, u64 size)
		{
			seAssert(offset % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT == 0, "Invalid offset");
            seAssert(size > 0, "Invalid size");
            seAssert(buffer != nullptr, "Invalid buffer");
			seAssert(buffer->get().Get() != nullptr, "Invalid buffer");

			u32 idx = cbv_srv_uav_descriptor_heap->allocate();
			CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_handle(cbv_srv_uav_descriptor_heap->get_cpu_handle_heap_start(), idx, cbv_srv_uav_descriptor_heap->get_increment_size());

			D3D12_CONSTANT_BUFFER_VIEW_DESC CBVDesc;
			CBVDesc.BufferLocation = buffer->get()->GetGPUVirtualAddress() + offset;
			CBVDesc.SizeInBytes = size;

			device->CreateConstantBufferView(&CBVDesc, cpu_handle);

            ConstantBufferView cbv;
            cbv.cbv = idx;
			return cbv;
		}

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

            for (u32 i = 0; i < swap_chain_buffer_count; i++)
            {
                SharedPtr<Texture> texture_resource = SharedPtr<Texture>(new Texture());
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
            swap_chain->Present(1, 0);

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
					heap->SetName(L"CBV SRV UAV Heap");
					u32 increment_size = device->GetDescriptorHandleIncrementSize(cbvSrvHeapDesc.Type);
					cbv_srv_uav_descriptor_heap = Ptr<DescriptorHeap>(new DescriptorHeap(heap, cbvSrvHeapDesc.NumDescriptors, increment_size));
				}
				{
					cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
					ComPtr<ID3D12DescriptorHeap> heap;
					CHECKHR(device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&heap)));
					heap->SetName(L"CBV SRV UAV Heap");
					u32 increment_size = device->GetDescriptorHandleIncrementSize(cbvSrvHeapDesc.Type);
					imgui_cbv_srv_uav_descriptor_heap = Ptr<DescriptorHeap>(new DescriptorHeap(heap, cbvSrvHeapDesc.NumDescriptors, increment_size));
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
                heap->SetName(L"RTV Heap");
                u32 increment_size = device->GetDescriptorHandleIncrementSize(rtvDesc.Type);
                rtv_descriptor_heap = Ptr<DescriptorHeap>(new DescriptorHeap(heap, rtvDesc.NumDescriptors, increment_size));
            }
            //dsv
            {
                ComPtr<ID3D12DescriptorHeap> heap;
                D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
                dsvDesc.NumDescriptors = DESCRIPTOR_COUNT;
                dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
                dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                CHECKHR(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&heap)));
                heap->SetName(L"DSV Heap");
                u32 increment_size = device->GetDescriptorHandleIncrementSize(dsvDesc.Type);
                dsv_descriptor_heap = Ptr<DescriptorHeap>(new DescriptorHeap(heap, dsvDesc.NumDescriptors, increment_size));
            }
            //sampler
            {
                ComPtr<ID3D12DescriptorHeap> heap;
                D3D12_DESCRIPTOR_HEAP_DESC splrDesc = {};
                splrDesc.NumDescriptors = DESCRIPTOR_COUNT;
                splrDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
                splrDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                CHECKHR(device->CreateDescriptorHeap(&splrDesc, IID_PPV_ARGS(&heap)));
                heap->SetName(L"Sampler Heap");
                u32 increment_size = device->GetDescriptorHandleIncrementSize(splrDesc.Type);
                sampler_descriptor_heap = Ptr<DescriptorHeap>(new DescriptorHeap(heap, splrDesc.NumDescriptors, increment_size));
            }
        }
	}
}