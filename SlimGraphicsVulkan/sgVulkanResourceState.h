#pragma once
#include "sgVulkanInclude.h"

namespace sg
{
	namespace Vulkan
	{
		// Mirrors the D3D12 backend's simple model: resources rest in a 'read' state and are transitioned to a specific state
		// for a write (UAV, render target, copy) then returned. Each state maps to the stages/accesses/layout used in barriers.
		enum class ResourceState
		{
			Undefined,
			Read,				// Shader/vertex/index/uniform/indirect reads (buffers) or sampled reads (textures)
			UnorderedAccess,	// Storage read/write
			RenderTarget,
			DepthWrite,
			CopySource,
			CopyDest,
			IndirectArgument,
			Present,
			HostRead,			// Readback memory, read by the CPU after the GPU has finished
		};

		struct StateInfo
		{
			vk::PipelineStageFlags2 stages;
			vk::AccessFlags2 access;
			vk::ImageLayout layout;
		};

		// 'shader_stages' is the set of shader stages supported by the device (mesh/task/geometry only when available)
		inline StateInfo get_state_info(ResourceState state, vk::PipelineStageFlags2 shader_stages)
		{
			using S = vk::PipelineStageFlagBits2;
			using A = vk::AccessFlagBits2;
			switch (state)
			{
			case ResourceState::Undefined:
				return { S::eNone, A::eNone, vk::ImageLayout::eUndefined };
			case ResourceState::Read:
				return { shader_stages | S::eVertexAttributeInput | S::eIndexInput | S::eDrawIndirect,
						 A::eShaderRead | A::eUniformRead | A::eVertexAttributeRead | A::eIndexRead | A::eIndirectCommandRead,
						 vk::ImageLayout::eReadOnlyOptimal };
			case ResourceState::UnorderedAccess:
				return { shader_stages, A::eShaderStorageRead | A::eShaderStorageWrite, vk::ImageLayout::eGeneral };
			case ResourceState::RenderTarget:
				return { S::eColorAttachmentOutput, A::eColorAttachmentRead | A::eColorAttachmentWrite, vk::ImageLayout::eAttachmentOptimal };
			case ResourceState::DepthWrite:
				return { S::eEarlyFragmentTests | S::eLateFragmentTests, A::eDepthStencilAttachmentRead | A::eDepthStencilAttachmentWrite, vk::ImageLayout::eAttachmentOptimal };
			case ResourceState::CopySource:
				return { S::eTransfer, A::eTransferRead, vk::ImageLayout::eTransferSrcOptimal };
			case ResourceState::CopyDest:
				return { S::eTransfer, A::eTransferWrite, vk::ImageLayout::eTransferDstOptimal };
			case ResourceState::IndirectArgument:
				return { S::eDrawIndirect, A::eIndirectCommandRead, vk::ImageLayout::eUndefined };
			case ResourceState::Present:
				// Ordering with the presentation engine is handled by the acquire/present semaphores (waited/signalled at ALL_COMMANDS)
				return { S::eAllCommands, A::eNone, vk::ImageLayout::ePresentSrcKHR };
			case ResourceState::HostRead:
				return { S::eHost, A::eHostRead, vk::ImageLayout::eUndefined };
			}
			seAssert(false, "Missing resource state");
			return { S::eAllCommands, A::eMemoryRead | A::eMemoryWrite, vk::ImageLayout::eGeneral };
		}
	}
}
