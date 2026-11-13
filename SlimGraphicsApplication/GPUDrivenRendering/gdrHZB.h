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
		
		void generate_hzb(sg::SharedPtr<sg::Device>& device, sg::CommandList& command_list, sg::ShaderResourceView depth_srv);

		sg::SharedPtr<sg::Texture> get_texture() const { return hzb_texture; }

	private:
		sg::SharedPtr<sg::Pipeline> hzb_pipeline;
		sg::SharedPtr<sg::Texture> hzb_texture;
	};
}