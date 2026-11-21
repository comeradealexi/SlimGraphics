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

		vk::ImageViewCreateInfo Texture::make_view_create_info(const TextureViewKey& key) const
		{
			vk::ImageViewCreateInfo create_info({}, image_handle, key.type, key.format, {}, vk::ImageSubresourceRange(key.aspect, key.base_mip, key.mip_count, key.base_layer, key.layer_count));
			if (resource_create_desc.format == DXGI_FORMAT_A8_UNORM)
			{
				// DXGI A8 maps to R8; expose it in the alpha channel like D3D does
				create_info.components = vk::ComponentMapping(vk::ComponentSwizzle::eZero, vk::ComponentSwizzle::eZero, vk::ComponentSwizzle::eZero, vk::ComponentSwizzle::eR);
			}
			return create_info;
		}

		vk::ImageView Texture::get_view(const TextureViewKey& key)
		{
			auto it = views.find(key);
			if (it != views.end())
			{
				return *it->second;
			}
			auto inserted = views.emplace(key, CHECKVK(owner.get_device().createImageView(make_view_create_info(key))));
			return *inserted.first->second;
		}

		const uint8_t* Texture::get_descriptor(const TextureViewKey& key, vk::DescriptorType type)
		{
			auto& cache = type == vk::DescriptorType::eStorageImage ? storage_descriptors : sampled_descriptors;
			auto it = cache.find(key);
			if (it == cache.end())
			{
				std::vector<uint8_t> bytes(owner.get_descriptor_slot_size());
				const vk::ImageLayout layout = type == vk::DescriptorType::eStorageImage ? vk::ImageLayout::eGeneral : vk::ImageLayout::eReadOnlyOptimal;
				owner.write_image_descriptor(bytes.data(), type, make_view_create_info(key), layout);
				it = cache.emplace(key, std::move(bytes)).first;
			}
			return it->second.data();
		}

		TextureViewKey Texture::make_shader_resource_view_key(u32 top_mip, u32 mip_count) const
		{
			TextureViewKey key;
			switch (resource_create_desc.dimension)
			{
			case ResourceDimension::Texture1D: key.type = vk::ImageViewType::e1D; break;
			case ResourceDimension::Texture3D: key.type = vk::ImageViewType::e3D; break;
			case ResourceDimension::Texture2DArray: key.type = vk::ImageViewType::e2DArray; key.layer_count = get_layer_count(); break;
			default: key.type = vk::ImageViewType::e2D; break; // Other multi slice resources are viewed as their first slice, as in D3D12
			}
			key.format = vk_format;
			// Sample the depth aspect of depth/stencil formats
			key.aspect = (aspect & vk::ImageAspectFlagBits::eDepth) ? vk::ImageAspectFlags(vk::ImageAspectFlagBits::eDepth) : aspect;
			key.base_mip = top_mip;
			key.mip_count = mip_count == static_cast<u32>(-1) ? VK_REMAINING_MIP_LEVELS : mip_count;
			return key;
		}

		TextureViewKey Texture::make_unordered_access_view_key(u32 mip) const
		{
			TextureViewKey key;
			key.type = resource_create_desc.dimension == ResourceDimension::Texture3D ? vk::ImageViewType::e3D : vk::ImageViewType::e2D;
			key.format = vk_format;
			key.aspect = aspect;
			key.base_mip = mip;
			key.mip_count = 1;
			return key;
		}

		TextureViewKey Texture::make_attachment_view_key(DXGI_FORMAT format) const
		{
			TextureViewKey key;
			key.type = vk::ImageViewType::e2D;
			key.format = format == DXGI_FORMAT_UNKNOWN ? vk_format : translate(format);
			key.aspect = aspect;
			key.base_mip = 0;
			key.mip_count = 1;
			return key;
		}

		void Texture::set_external_image(vk::Image external_image, bool is_swap_chain_image)
		{
			views.clear();
			sampled_descriptors.clear();
			storage_descriptors.clear();
			image_handle = external_image;
			swap_chain_buffer = is_swap_chain_image;
		}
	}
}
