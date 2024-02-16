#include "pch.h"
#include "sgD3D12Device.h"
#include "sgD3D12CommandQueue.h"
#include "sgD3D12CommandList.h"

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

        Ptr<Memory> Device::allocate_memory(MemoryType type, MemorySubType sub_type, u64 size, u64 alignment)
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

            return Ptr<Memory>(new Memory(out_alloc.Get()));
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
            return Ptr<CommandList>(new CommandList(command_list, command_allocator));
        }

        Ptr<VertexShader> Device::create_vertex_shader(uint8_t* data, u64 size)
        {
            CD3DX12_SHADER_BYTECODE code(data, size);
            return Ptr<VertexShader>(new VertexShader(code));
        }

        Ptr<PixelShader> Device::create_pixel_shader(uint8_t* data, u64 size)
        {
            CD3DX12_SHADER_BYTECODE code(data, size);
            return Ptr<PixelShader>(new PixelShader(code));
        }

        bool Device::create_swap_chain(HWND hwnd, CommandQueue* command_queue, u32 buffer_count, DXGI_FORMAT format, u32 width, u32 height, Ptr<RenderTargetView>* rtv_list)
        {
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.BufferCount = buffer_count;
            desc.Width = width;
            desc.Height = height;
            desc.Format = format;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.SampleDesc.Count = 1;
            HRESULT hr = factory->CreateSwapChainForHwnd(command_queue->get().Get(), hwnd, &desc, nullptr, nullptr, swap_chain.GetAddressOf());
            CHECKHR(hr);

            for (u32 i = 0; i < buffer_count; i++)
            {
                CHECKHR(swap_chain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
                device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
            }

            return SUCCEEDED(hr);
        }

        void Device::create_descriptors()
        {
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
                cbv_srv_uav_descriptor_heap = Ptr<DescriptorHeap>(new DescriptorHeap(heap, cbvSrvHeapDesc.NumDescriptors, increment_size));
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
                dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
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
                splrDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                splrDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                CHECKHR(device->CreateDescriptorHeap(&splrDesc, IID_PPV_ARGS(&heap)));
                heap->SetName(L"Sampler Heap");
                u32 increment_size = device->GetDescriptorHandleIncrementSize(splrDesc.Type);
                sampler_descriptor_heap = Ptr<DescriptorHeap>(new DescriptorHeap(heap, splrDesc.NumDescriptors, increment_size));
            }
        }
	}
}