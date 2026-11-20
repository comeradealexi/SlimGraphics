#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"
#include "sgVulkanResourceState.h"
#include <unordered_map>

namespace sg
{
	namespace Vulkan
	{
		// Describes an image view; views are created on demand and cached by the texture that owns them.
		struct TextureViewKey
		{
			vk::ImageViewType type = vk::ImageViewType::e2D;
			vk::Format format = vk::Format::eUndefined;
			vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
			u32 base_mip = 0;
			u32 mip_count = VK_REMAINING_MIP_LEVELS;
			u32 base_layer = 0;
			u32 layer_count = 1;

			bool operator==(const TextureViewKey& rhs) const
			{
				return type == rhs.type && format == rhs.format && aspect == rhs.aspect && base_mip == rhs.base_mip && mip_count == rhs.mip_count && base_layer == rhs.base_layer && layer_count == rhs.layer_count;
			}
		};

		struct TextureViewKeyHash
		{
			size_t operator()(const TextureViewKey& k) const
			{
				size_t h = static_cast<size_t>(k.type) * 31u + static_cast<size_t>(k.format);
				h = h * 31u + static_cast<VkImageAspectFlags>(k.aspect);
				h = h * 31u + k.base_mip;
				h = h * 31u + k.mip_count;
				h = h * 31u + k.base_layer;
				return h * 31u + k.layer_count;
			}
		};

		class Texture
		{
			friend class Device;
		public:
			Texture(Device& owner, const ResourceCreateDesc& rcd, bool is_swap_chain_buffer = false);
			~Texture();
			Texture(const Texture&) = delete;
			Texture& operator=(const Texture&) = delete;

			vk::Image get() const { return image_handle; }
			size_t get_size_bytes() const { return size_bytes; }
			MemoryType get_memory_type() const { return MemoryType::GPUOptimal; }
			vk::Format get_vk_format() const { return vk_format; }
			vk::ImageAspectFlags get_aspect() const { return aspect; }
			vk::ImageSubresourceRange get_full_range() const { return vk::ImageSubresourceRange(aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS); }
			vk::Extent3D get_mip_extent(u32 mip) const;
			u32 get_layer_count() const;
			bool is_swap_chain() const { return swap_chain_buffer; }

			// State the texture rests in between uses (see ResourceState)
			ResourceState get_read_resource_state() const { return swap_chain_buffer ? ResourceState::Present : ResourceState::Read; }

			vk::ImageView get_view(const TextureViewKey& key);
			// Default view for sampling: all (or the given) mips of the first layer/slice
			vk::ImageView get_shader_resource_view(u32 top_mip = 0, u32 mip_count = VK_REMAINING_MIP_LEVELS);
			// Single mip view for storage access
			vk::ImageView get_unordered_access_view(u32 mip);
			// Single mip view for attachment use
			vk::ImageView get_attachment_view(DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

			const ResourceCreateDesc resource_create_desc;

		private:
			// Swap chain images are owned by the swap chain. While the window is minimised a swap chain texture is pointed at an
			// ordinary image instead (which rests in the read state rather than PRESENT).
			void set_external_image(vk::Image image, bool is_swap_chain_image);

			Device& owner;
			bool swap_chain_buffer;
			const vk::Format vk_format;
			const vk::ImageAspectFlags aspect;
			size_t size_bytes = 0;
			vk::Image image_handle = VK_NULL_HANDLE;
			SharedPtr<Memory> memory;
			vk::raii::Image image = nullptr;
			std::unordered_map<TextureViewKey, vk::raii::ImageView, TextureViewKeyHash> views; // Destroyed before the image
		};
	}
}
