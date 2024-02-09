#pragma once
#include <sgTypes.h>
#include "sgD3D12Device.h"
#include "sgD3D12Memory.h"

#define SG_PLATFORM_D3D12 (1)

namespace sg
{
	using Device = D3D12::Device;
	using Memory = D3D12::Memory;
}