#pragma once
#include <sgTypes.h>
#include "sgD3D12Include.h"

namespace sg
{
	namespace D3D12
	{
		D3D12_RASTERIZER_DESC translate(const Rasterizer::Desc& desc)
		{
			D3D12_RASTERIZER_DESC o = {};

			return o;
		}

		D3D12_DEPTH_STENCIL_DESC translate(const DepthStencil::Desc& desc)
		{
			D3D12_DEPTH_STENCIL_DESC o = {};

			return o;
		}

		D3D12_BLEND_DESC translate(const Blend::Desc& desc)
		{
			D3D12_BLEND_DESC o = {};

			return o;
		}
	}
}