#pragma once
#include "sgPlatformForwardDeclare.h"
#include <sgTypes.h>
#include "sgD3D12Device.h"
#include "sgD3D12Shader.h"
#include "sgD3D12Memory.h"
#include "sgD3D12Binding.h"
#include "sgD3D12CommandList.h"
#include "sgD3D12CommandQueue.h"
#include "sgD3D12Buffer.h"
#include "sgD3D12Texture.h"
#include "sgD3D12ShaderResourceView.h"
#include "sgD3D12BufferView.h"
#include "sgD3D12Pipeline.h"
#include "sgD3D12RenderTargetView.h"
#include "sgD3D12ConstantBufferView.h"
#include "sgD3D12VertexBufferView.h"
#include "sgD3D12IndexBufferView.h"
#include "sgD3D12UnorderedAccessView.h"
#include "sgD3D12GPUTimestampPool.h"
#include "sgD3D12GPUStatisticPool.h"

#ifndef SG_PLATFORM_D3D12
#error Expecting SG_PLATFORM_D3D12 to be defined
#endif