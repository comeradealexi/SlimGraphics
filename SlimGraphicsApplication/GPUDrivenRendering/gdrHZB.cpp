#include "gdrHZB.h"
#include "sgUtils.h"
#include <Utils.h>
#include <bit>

using namespace sg;

namespace gdr
{
	HZB::HZB(sg::SharedPtr<sg::Texture> depth_texture, sg::SharedPtr<sg::Device>& device)
	{
		BindingDesc pipeline_binding_desc = {};
		pipeline_binding_desc.cbv_binding_count = 1;
		pipeline_binding_desc.srv_binding_count = 1;
		pipeline_binding_desc.uav_binding_count = MIPS_PER_PASS + 1;

		PipelineDesc::Compute desc;
		desc.compute_shader = create_compute_shader(*device, get_shader_path("GDR_BuildHZB_SRV"));
		hzb_pipeline_first_sample_is_depth = device->create_pipeline(desc, pipeline_binding_desc);
		seAssert(hzb_pipeline_first_sample_is_depth != nullptr, "Failed to create pipeline");

		desc.compute_shader = create_compute_shader(*device, get_shader_path("GDR_BuildHZB"));
		hzb_pipeline_stage2 = device->create_pipeline(desc, pipeline_binding_desc);
		seAssert(hzb_pipeline_stage2 != nullptr, "Failed to create pipeline");

		// Create the HZB texture.
		// Mip 0 is the depth size rounded up to a power of two so that every level is exactly half the previous one and
		// each HZB texel covers a clean 2x2 footprint (no dropped rows/columns for odd sizes). The region past the depth
		// size is filled with clamped edge depth. Consumers should map screen UVs with depth_size / hzb_size.
		sg::ResourceCreateDesc hzb_texture_desc = depth_texture->resource_create_desc;
		hzb_texture_desc.usage_flags = ResourceUsageFlags::UnorderedAccess;
		hzb_texture_desc.width = std::bit_ceil(std::max<u32>(hzb_texture_desc.width, 1u));
		hzb_texture_desc.height = std::bit_ceil(std::max<u32>(hzb_texture_desc.height, 1u));

		// Calculate mip count for HZB texture
		u32 w = hzb_texture_desc.width;
		u32 h = hzb_texture_desc.height;
		u32 mip_count = 1;
		while (w > 1 || h > 1)
		{
			w = std::max(1u, w / 2);
			h = std::max(1u, h / 2);
			mip_count++;
		}
		hzb_texture_desc.mip_count = mip_count;
		hzb_texture_desc.format = DXGI_FORMAT_R32_FLOAT;
		SizeAndAlignment sal = device->calculate_resource_size_alignment(hzb_texture_desc);
		auto hzb_memory = device->allocate_memory(sg::MemoryType::GPUOptimal, sg::MemorySubType::Texture, sal.size, sal.alignment);
		hzb_texture = device->create_texture(hzb_memory, (u32)sal.size, hzb_texture_desc);
	}

	void HZB::generate_hzb(sg::SharedPtr<sg::Device>& device, sg::CommandList& command_list, sg::ShaderResourceView depth_srv, SimpleLinearConstantBuffer& cbuffer)
	{
		const u32 mip_count = hzb_texture->resource_create_desc.mip_count;
		auto ceil_divide = [](u32 a, u32 b) -> u32 { return (a + b - 1) / b; };
		auto mip_size = [](u32 size, u32 mip) -> u32 { return std::max(1u, size >> mip); };

		struct HZBConstantData
		{
			u32 output_mip_count;
			u32 pad[3];
		};

		// Each pass reads one level (u0) and writes up to MIPS_PER_PASS coarser levels (u1..u5).
		// The first pass reads the depth SRV and writes mip 0 as well.
		u32 source_mip = 0;
		bool first_pass = true;
		while (first_pass || source_mip + 1 < mip_count)
		{
			const u32 outputs = std::min<u32>(MIPS_PER_PASS, mip_count - 1 - source_mip);

			Binding b = {};
			b.cbv_binding_count = 1;
			b.srv_binding_count = 1;
			b.uav_binding_count = MIPS_PER_PASS + 1;

			HZBConstantData constant_data = {};
			constant_data.output_mip_count = outputs;
			ConstantBufferView cbv = cbuffer.AllocateAndWrite(constant_data);
			b.set_cbv(cbv, 0);
			b.set_srv(depth_srv, 0);

			// Slots past the last real output are bound to the last written level; the shader does not write them.
			const u32 last_valid_mip = source_mip + std::max(outputs, 0u);
			for (u32 slot = 0; slot <= MIPS_PER_PASS; slot++)
			{
				const u32 mip = std::min(source_mip + slot, last_valid_mip);
				UnorderedAccessView uav = device->create_unordered_access_view(hzb_texture, mip);
				b.set_uav(uav, slot);
			}

			command_list.set_pipeline(first_pass ? hzb_pipeline_first_sample_is_depth.get() : hzb_pipeline_stage2.get());
			command_list.bind(b, PipelineType::Compute);
			const u32 source_width = mip_size(hzb_texture->resource_create_desc.width, source_mip);
			const u32 source_height = mip_size(hzb_texture->resource_create_desc.height, source_mip);
			command_list.dispatch(ceil_divide(source_width, TILE_SIZE), ceil_divide(source_height, TILE_SIZE), 1);

			if (outputs == 0)
			{
				break; // 1x1 HZB: only mip 0 exists
			}
			source_mip += outputs;
			first_pass = false;
		}
	}
}
