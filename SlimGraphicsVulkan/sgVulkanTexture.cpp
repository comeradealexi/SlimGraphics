#include "pch.h"
#include "sgVulkanTexture.h"
#include "sgVulkanMemory.h"
#include "sgVulkanTypesTranslator.h"
#include "sgVulkanDevice.h"

namespace sg
{
	namespace Vulkan
	{
		Texture::Texture(Device& owner_, const ResourceCreateDesc& rcd, bool is_swap_chain_buffer)
			: resource_create_desc(rcd), owner(owner_), swap_chain_buffer(is_swap_chain_buffer), vk_format(translate(rcd.format)), aspect(get_aspect_flags(rcd.format))
		{
		}

		Texture::~Texture()
		{
			if (*image)
			{
				// The image may be destroyed before its first-use transition was submitted
				owner.remove_pending_initial_transition(*image);
			}
		}

		vk::Extent3D Texture::get_mip_extent(u32 mip) const
		{
			const u32 depth = resource_create_desc.dimension == ResourceDimension::Texture3D ? resource_create_desc.depth : 1;
			return vk::Extent3D(std::max(1u, resource_create_desc.width >> mip), std::max(1u, resource_create_desc.height >> mip), std::max(1u, depth >> mip));
		}

		u32 Texture::get_layer_count() const
		{
			// ResourceCreateDesc::depth is DepthOrArraySize, as in D3D12
			return resource_create_desc.dimension == ResourceDimension::Texture3D ? 1u : std::max(1u, resource_create_desc.depth);
		}

		vk::ImageView Texture::get_view(const TextureViewKey& key)
		{
			auto it = views.find(key);
			if (it != views.end())
			{
				return *it->second;
			}

			vk::ImageViewCreateInfo create_info({}, image_handle, key.type, key.format, {}, vk::ImageSubresourceRange(key.aspect, key.base_mip, key.mip_count, key.base_layer, key.layer_count));
			if (resource_create_desc.format == DXGI_FORMAT_A8_UNORM)
			{
				// DXGI A8 maps to R8; expose it in the alpha channel like D3D does
				create_info.components = vk::ComponentMapping(vk::ComponentSwizzle::eZero, vk::ComponentSwizzle::eZero, vk::ComponentSwizzle::eZero, vk::ComponentSwizzle::eR);
			}
			auto inserted = views.emplace(key, vk::raii::ImageView(owner.get_device(), create_info));
			return *inserted.first->second;
		}

		vk::ImageView Texture::get_shader_resource_view(u32 top_mip, u32 mip_count)
		{
			TextureViewKey key;
			switch (resource_create_desc.dimension)
			{
			case ResourceDimension::Texture1D: key.type = vk::ImageViewType::e1D; break;
			case ResourceDimension::Texture3D: key.type = vk::ImageViewType::e3D; break;
			default: key.type = vk::ImageViewType::e2D; break; // Array/cube resources are viewed as their first slice, as in D3D12
			}
			key.format = vk_format;
			// Sample the depth aspect of depth/stencil formats
			key.aspect = (aspect & vk::ImageAspectFlagBits::eDepth) ? vk::ImageAspectFlags(vk::ImageAspectFlagBits::eDepth) : aspect;
			key.base_mip = top_mip;
			key.mip_count = mip_count == static_cast<u32>(-1) ? VK_REMAINING_MIP_LEVELS : mip_count;
			return get_view(key);
		}

		vk::ImageView Texture::get_unordered_access_view(u32 mip)
		{
			TextureViewKey key;
			key.type = resource_create_desc.dimension == ResourceDimension::Texture3D ? vk::ImageViewType::e3D : vk::ImageViewType::e2D;
			key.format = vk_format;
			key.aspect = aspect;
			key.base_mip = mip;
			key.mip_count = 1;
			return get_view(key);
		}

		vk::ImageView Texture::get_attachment_view(DXGI_FORMAT format)
		{
			TextureViewKey key;
			key.type = vk::ImageViewType::e2D;
			key.format = format == DXGI_FORMAT_UNKNOWN ? vk_format : translate(format);
			key.aspect = aspect;
			key.base_mip = 0;
			key.mip_count = 1;
			return get_view(key);
		}

		void Texture::set_external_image(vk::Image external_image, bool is_swap_chain_image)
		{
			views.clear();
			image_handle = external_image;
			swap_chain_buffer = is_swap_chain_image;
		}
	}
}
