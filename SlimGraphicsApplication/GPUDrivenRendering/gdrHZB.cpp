#include "gdrHZB.h"
#include "sgUtils.h"
#include <Utils.h>

using namespace sg;

namespace gdr
{
	HZB::HZB(sg::SharedPtr<sg::Texture> depth_texture, sg::SharedPtr<sg::Device>& device)
	{
		BindingDesc pipeline_binding_desc = {};
		pipeline_binding_desc.srv_binding_count = 1;
		pipeline_binding_desc.uav_binding_count = 6;

		PipelineDesc::Compute desc;
		desc.compute_shader = create_compute_shader(*device, get_shader_path("GDR_BuildHZB_SRV"));
		hzb_pipeline_first_sample_is_depth = device->create_pipeline(desc, pipeline_binding_desc);
		seAssert(hzb_pipeline_first_sample_is_depth != nullptr, "Failed to create pipeline");

		desc.compute_shader = create_compute_shader(*device, get_shader_path("GDR_BuildHZB"));
		hzb_pipeline_stage2 = device->create_pipeline(desc, pipeline_binding_desc);
		seAssert(hzb_pipeline_stage2 != nullptr, "Failed to create pipeline");

		// Create the HZB texture
		sg::ResourceCreateDesc hzb_texture_desc = depth_texture->resource_create_desc;
		hzb_texture_desc.usage_flags = ResourceUsageFlags::UnorderedAccess;

		// Calculate mip count for HZB texture
		u32 w = hzb_texture_desc.width;
		u32 h = hzb_texture_desc.height;
		u32 mip_count = 1;
		while (w > 1 || h > 1)
		{
			w = w / 2;
			h = h / 2;
			mip_count++;
		}
		hzb_texture_desc.mip_count = mip_count;
		hzb_texture_desc.format = DXGI_FORMAT_R32_FLOAT;
		SizeAndAlignment sal = device->calculate_resource_size_alignment(hzb_texture_desc);
		auto hzb_memory = device->allocate_memory(sg::MemoryType::GPUOptimal, sg::MemorySubType::Texture, sal.size, sal.alignment);
		hzb_texture = device->create_texture(hzb_memory, sal.size, hzb_texture_desc);
	}

	void HZB::generate_hzb(sg::SharedPtr<sg::Device>& device, sg::CommandList& command_list, sg::ShaderResourceView depth_srv)
	{
		const u32 mips_to_generate = hzb_texture->resource_create_desc.mip_count - 1;

		Binding b = {};
		b.srv_binding_count = 1;
		b.uav_binding_count = 6;

		b.set_srv(depth_srv, 0);

		auto hzb_uav_0 = device->create_unordered_access_view(hzb_texture, 0); // 1920x1080
		auto hzb_uav_1 = device->create_unordered_access_view(hzb_texture, 1); // 960x540
		auto hzb_uav_2 = device->create_unordered_access_view(hzb_texture, 2); // 480x270
		auto hzb_uav_3 = device->create_unordered_access_view(hzb_texture, 3); // 240x135
		auto hzb_uav_4 = device->create_unordered_access_view(hzb_texture, 4); // 120x67
		auto hzb_uav_5 = device->create_unordered_access_view(hzb_texture, 5); // 60x33
		//auto hzb_uav_6 = device->create_unordered_access_view(hzb_texture, 6); // 30x17
		
		b.set_uav(hzb_uav_0, 0);
		b.set_uav(hzb_uav_1, 1);
		b.set_uav(hzb_uav_2, 2);
		b.set_uav(hzb_uav_3, 3);
		b.set_uav(hzb_uav_4, 4);
		b.set_uav(hzb_uav_5, 5);
		//b.set_uav(hzb_uav_6, 6);

		auto ceil_divide = [](u32 a, u32 b) -> u32 { return (a + b - 1) / b; };

		command_list.set_pipeline(hzb_pipeline_first_sample_is_depth.get());
		command_list.bind(b, PipelineType::Compute);
		command_list.dispatch(ceil_divide(hzb_texture->resource_create_desc.width, 32), ceil_divide(hzb_texture->resource_create_desc.height, 32), 1);

		//hzb_uav_0 = device->create_unordered_access_view(hzb_texture, 3); // 240x135
		//hzb_uav_1 = device->create_unordered_access_view(hzb_texture, 4); // 120x67
		//hzb_uav_2 = device->create_unordered_access_view(hzb_texture, 5); // 60x33
		//hzb_uav_3 = device->create_unordered_access_view(hzb_texture, 6); // 30x17
		//
		//b.set_uav(hzb_uav_0, 0);
		//b.set_uav(hzb_uav_1, 1);
		//b.set_uav(hzb_uav_2, 2);
		//b.set_uav(hzb_uav_3, 3);
		//
		//command_list.set_pipeline(hzb_pipeline_stage2.get());
		//command_list.bind(b, PipelineType::Compute);
		//command_list.dispatch((hzb_texture->resource_create_desc.width / 8) / 8, (hzb_texture->resource_create_desc.height / 8) / 8, 1);

		//hzb_uav_0 = device->create_unordered_access_view(hzb_texture, 0); // 30x17
		//hzb_uav_1 = device->create_unordered_access_view(hzb_texture, 1); // 15x9
		//hzb_uav_2 = device->create_unordered_access_view(hzb_texture, 2); // 7x5
		//hzb_uav_3 = device->create_unordered_access_view(hzb_texture, 3); // 3x3
		//
		//command_list.bind(b, PipelineType::Compute);
		//command_list.dispatch((hzb_texture->resource_create_desc.width / 8) / 16, (hzb_texture->resource_create_desc.height / 8) / 16, 1);
	}
}