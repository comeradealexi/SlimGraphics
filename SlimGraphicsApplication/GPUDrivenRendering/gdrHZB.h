#pragma once
#include <array>
#include <sgPlatformInclude.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <unordered_map>
#include "sgUploadHeap.h"
#include "gdrShaderStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "DebugDraw.h"
#include "gdrJSON.h"
#include "gdrEditor.h"
#include "gdrModel.h"

namespace gdr
{
	using sg::Ptr;
	using sg::UploadHeap;

	class HZB
	{
	public:
		HZB(sg::SharedPtr<sg::Texture> depth_texture, sg::SharedPtr<sg::Device>& device);

		// Builds the full HZB mip chain from the depth buffer (stores the farthest depth per texel).
		void generate_hzb(sg::SharedPtr<sg::Device>& device, sg::CommandList& command_list, sg::ShaderResourceView depth_srv, SimpleLinearConstantBuffer& cbuffer);

		sg::SharedPtr<sg::Texture> get_texture() const { return hzb_texture; }

	private:
		static constexpr sg::u32 TILE_SIZE = 32;		// Must match TILE_SIZE in gdrHZB.hlsl
		static constexpr sg::u32 MIPS_PER_PASS = 5;	// Levels written per dispatch (u1..u5)

		sg::SharedPtr<sg::Pipeline> hzb_pipeline_first_sample_is_depth; // Samples SRV
		sg::SharedPtr<sg::Pipeline> hzb_pipeline_stage2;
		sg::SharedPtr<sg::Texture> hzb_texture;
	};
}
