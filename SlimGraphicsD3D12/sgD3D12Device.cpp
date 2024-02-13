#include "pch.h"
#include "sgD3D12Device.h"
#include "sgD3D12CommandQueue.h"
#include "sgD3D12CommandList.h"

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

		Device::Device()
		{
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

            ComPtr<IDXGIFactory4> factory;
            CHECKHR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

            ComPtr<IDXGIAdapter1> hardware_adapter;
            GetHardwareAdapter(factory.Get(), &hardware_adapter, true, feature_level);

            CHECKHR(D3D12CreateDevice(hardware_adapter.Get(), feature_level, IID_PPV_ARGS(&device)));
            CHECKHR(device->QueryInterface(IID_PPV_ARGS(&device6)));

            features.Init(device.Get());
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
	}
}