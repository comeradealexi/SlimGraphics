#pragma once
#include "sgPlatformForwardDeclare.h"
#include <sgTypes.h>
#include "sgD3D12Device.h"
#include "sgD3D12Memory.h"
#include "sgD3D12Shader.h"
#include "sgD3D12CommandList.h"
#include "sgD3D12CommandQueue.h"
#include "sgD3D12Buffer.h"
#include "sgD3D12Texture.h"
#include "sgD3D12ShaderResourceView.h"
#include "sgD3D12BufferView.h"
#include "sgD3D12Pipeline.h"

#ifndef SG_PLATFORM_D3D12
#error Expecting SG_PLATFORM_D3D12 to be defined
#endif