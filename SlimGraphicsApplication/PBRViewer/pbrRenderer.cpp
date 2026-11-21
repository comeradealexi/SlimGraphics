#include "pbrRenderer.h"
#include "CVars.h"
#include <sgUtils.h>
#include <Utils.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cctype>

using namespace DirectX;

namespace pbr
{
	namespace
	{
		// --- Settings (every one is a cvar: the "PBR Settings" window, -cvar=name=value, test plans)
		CVarInt cv_mode("pbr.mode", "General", "Real time (rasterised visibility buffer + ray traced lighting) or the progressive reference path tracer",
			0, { "Real Time", "Path Traced" }, CVarResetHistory);
		CVarInt cv_debug_view("pbr.debug_view", "General", "What the viewer shows",
			0, { "Final", "Albedo", "Normals", "Geometric Normals", "Roughness", "Metallic", "Ambient Occlusion", "Emissive", "Motion Vectors", "Depth",
			"Instances", "Meshlets", "Triangles", "Shadow (Raw)", "Shadow (Denoised)", "Diffuse Indirect (Raw)", "Diffuse Indirect (Denoised)",
			"Specular (Raw)", "Specular (Denoised)", "Direct Lighting", "Indirect Lighting", "History Length", "Diffuse Variance", "HZB", "Radiance Cache",
			"White Furnace" }, CVarResetHistory);

		CVarBool cv_cull_frustum("pbr.cull.frustum", "Culling", "Instance and meshlet frustum culling", true);
		CVarBool cv_cull_cone("pbr.cull.cone", "Culling", "Meshlet normal cone (back face) culling", true);
		CVarBool cv_cull_occlusion("pbr.cull.occlusion", "Culling", "Two phase HZB occlusion culling of instances and meshlets", true);
		CVarBool cv_cull_freeze("pbr.cull.freeze", "Culling", "Freeze the frustum culling camera (fly around to inspect what is culled)", false);

		CVarFloat cv_sun_elevation("pbr.sun.elevation", "Sun", "Degrees above the horizon", 60.0f, -10.0f, 90.0f, CVarResetHistory);
		CVarFloat cv_sun_azimuth("pbr.sun.azimuth", "Sun", "Degrees around the vertical axis", 30.0f, 0.0f, 360.0f, CVarResetHistory);
		CVarFloat cv_sun_illuminance("pbr.sun.illuminance", "Sun", "Solar illuminance at the top of the atmosphere (lux)", 128000.0f, 0.0f, 200000.0f, CVarResetHistory | CVarLogScale);
		CVarFloat cv_sun_radius("pbr.sun.angular_radius", "Sun", "Angular radius of the sun's disk in degrees (0.27 for the real sun; larger gives softer shadows)", 0.2678f, 0.01f, 5.0f, CVarResetHistory | CVarLogScale);
		CVarBool cv_sun_disk("pbr.sun.disk", "Sun", "Draw the sun's disk in the sky", true, CVarResetHistory);

		CVarFloat cv_sky_intensity("pbr.sky.intensity", "Sky", "Multiplier of the sky's radiance (1 = physically based)", 1.0f, 0.0f, 4.0f, CVarResetHistory);
		CVarFloat cv_sky_mie("pbr.sky.mie_density", "Sky", "Aerosol (haze) density relative to Earth's average", 1.0f, 0.0f, 10.0f, CVarResetHistory | CVarLogScale);
		CVarFloat cv_sky_mie_g("pbr.sky.mie_anisotropy", "Sky", "Forward scattering of aerosols (Mie phase function g)", 0.8f, 0.0f, 0.99f, CVarResetHistory);
		CVarFloat cv_sky_rayleigh("pbr.sky.rayleigh_density", "Sky", "Air (Rayleigh) density relative to Earth's", 1.0f, 0.0f, 4.0f, CVarResetHistory);
		CVarFloat cv_sky_ozone("pbr.sky.ozone_density", "Sky", "Ozone density relative to Earth's", 1.0f, 0.0f, 4.0f, CVarResetHistory);
		CVarFloat cv_sky_ground("pbr.sky.ground_albedo", "Sky", "Albedo of the ground below the horizon", 0.3f, 0.0f, 1.0f, CVarResetHistory);
		CVarBool cv_sky_lighting("pbr.sky.lighting", "Sky", "Light from the sky (rays that leave the scene)", true, CVarResetHistory);

		CVarBool cv_textures("pbr.material.textures", "Materials", "Material textures (off: material factors only)", true, CVarResetHistory);
		CVarBool cv_normal_maps("pbr.material.normal_maps", "Materials", "Normal maps", true, CVarResetHistory);
		CVarBool cv_alpha_test("pbr.material.alpha_test", "Materials", "Alpha testing of cut out materials", true, CVarResetHistory);
		CVarBool cv_emissive("pbr.material.emissive", "Materials", "Emissive surfaces", true, CVarResetHistory);
		CVarFloat cv_emissive_scale("pbr.material.emissive_scale", "Materials", "Emissive radiance per unit of material emission (nits)", 1000.0f, 0.0f, 100000.0f, CVarResetHistory | CVarLogScale);
		CVarFloat cv_roughness_min("pbr.material.roughness_min", "Materials", "Smallest perceptual roughness (avoids singular highlights)", 0.03f, 0.0f, 0.5f, CVarResetHistory);
		CVarFloat cv_roughness_override("pbr.material.roughness_override", "Materials", "Overrides every material's roughness (negative: off)", -1.0f, -1.0f, 1.0f, CVarResetHistory);
		CVarFloat cv_metallic_override("pbr.material.metallic_override", "Materials", "Overrides every material's metallic (negative: off)", -1.0f, -1.0f, 1.0f, CVarResetHistory);
		CVarBool cv_albedo_override_enable("pbr.material.albedo_override_enable", "Materials", "Overrides every material's base colour", false, CVarResetHistory);
		CVarFloat3 cv_albedo_override("pbr.material.albedo_override", "Materials", "Base colour used by the override", 0.5f, 0.5f, 0.5f, true, CVarResetHistory);
		CVarFloat cv_lod_bias("pbr.material.texture_lod_bias", "Materials", "Texture level of detail bias (log2)", 0.0f, -4.0f, 4.0f, CVarResetHistory);

		CVarBool cv_shadows("pbr.light.shadows", "Lighting", "Ray traced sun shadows", true, CVarResetHistory);
		CVarBool cv_diffuse_gi("pbr.light.diffuse_gi", "Lighting", "Ray traced diffuse global illumination", true, CVarResetHistory);
		CVarBool cv_specular("pbr.light.specular", "Lighting", "Ray traced reflections", true, CVarResetHistory);
		CVarBool cv_emissive_sampling("pbr.light.emissive_sampling", "Lighting", "Sample emissive triangles as lights (otherwise only rays that happen to hit them find their light)", true, CVarResetHistory);
		CVarInt cv_emissive_candidates("pbr.light.emissive_candidates", "Lighting", "Emissive light candidates per light sample at visible surfaces (and path tracer vertices), resampled by their unshadowed contribution (1: power weighted table only)", 8, 1, 64, CVarResetHistory);
		CVarInt cv_emissive_candidates_secondary("pbr.light.emissive_candidates_secondary", "Lighting", "Emissive light candidates per light sample at the hits of indirect rays", 2, 1, 64, CVarResetHistory);
		CVarBool cv_radiance_cache("pbr.light.radiance_cache", "Lighting", "Radiance cache: light reaching ray hits over further bounces", true, CVarResetHistory);
		CVarBool cv_emissive_at_hits("pbr.light.emissive_at_hits", "Lighting", "Emissive light samples at the hits of indirect rays (otherwise those rays only find emitters by hitting them; bounced lamp light is a small part of the image, so the default saves the rays)", false, CVarResetHistory);
		CVarBool cv_sun_shadows_at_hits("pbr.light.sun_shadows_at_hits", "Lighting", "Shadow rays for sunlight at the hits of indirect rays (otherwise bounced sunlight ignores occlusion: cheaper, too bright)", true, CVarResetHistory);
		CVarFloat cv_specular_max_roughness("pbr.light.specular_max_roughness", "Lighting", "Roughest surface that traces its own reflection ray; rougher ones reuse the diffuse ray (1: always trace)", 1.0f, 0.0f, 1.0f, CVarResetHistory);
		CVarFloat cv_firefly("pbr.light.firefly_clamp", "Lighting", "Largest exposed luminance of an indirect sample (0: off, unbiased)", 32.0f, 0.0f, 1000.0f, CVarResetHistory | CVarLogScale);
		CVarFloat cv_ray_offset("pbr.light.ray_offset", "Lighting", "Distance (metres) rays start above surfaces", 0.002f, 0.0001f, 0.05f, CVarResetHistory | CVarLogScale);
		CVarFloat cv_max_distance("pbr.light.max_ray_distance", "Lighting", "Longest ray (metres)", 10000.0f, 10.0f, 100000.0f, CVarResetHistory | CVarLogScale);
		CVarFloat cv_diffuse_spread("pbr.light.diffuse_cone_spread", "Lighting", "Ray cone spread of diffuse rays (texture level of detail at hits)", 0.3f, 0.0f, 1.0f, CVarResetHistory);
		CVarFloat cv_alpha_lod_bias("pbr.light.alpha_test_lod_bias", "Lighting", "Texture level bias of alpha tests at ray hits", 0.0f, 0.0f, 4.0f, CVarResetHistory);
		CVarInt cv_alpha_tests_primary("pbr.light.alpha_tests_shadow", "Lighting", "Most alpha tested candidates a primary shadow ray tests before treating the rest as solid (foliage cost)", 64, 1, 1024, CVarResetHistory);
		CVarInt cv_alpha_tests_secondary("pbr.light.alpha_tests_indirect", "Lighting", "Most alpha tested candidates other real time rays test (indirect light through foliage; the path tracer tests all)", 4, 1, 1024, CVarResetHistory);
		CVarFloat cv_specular_spread("pbr.light.specular_cone_spread", "Lighting", "Ray cone spread of reflection rays per unit of GGX alpha", 0.5f, 0.0f, 2.0f, CVarResetHistory);

		CVarFloat cv_cache_cell("pbr.cache.cell_size", "Radiance Cache", "Cell size per metre of distance from the camera", 0.02f, 0.001f, 0.2f, CVarResetHistory | CVarLogScale);
		CVarFloat cv_cache_min_cell("pbr.cache.min_cell_size", "Radiance Cache", "Smallest cell (metres)", 0.1f, 0.01f, 2.0f, CVarResetHistory | CVarLogScale);
		CVarBool cv_cache_propagation("pbr.cache.propagation", "Radiance Cache", "Rays from diffuse ray hits that feed the cache (it then also learns light around corners and behind the camera)", true, CVarResetHistory);
		CVarInt cv_cache_update_interval("pbr.cache.update_interval", "Radiance Cache", "One pixel in this many feeds the cache each frame (and traces a propagation ray)", 4, 1, 64, CVarResetHistory);
		CVarFloat cv_cache_blend("pbr.cache.blend", "Radiance Cache", "Weight of each frame's samples (lower: smoother, slower to react)", 0.05f, 0.005f, 1.0f, CVarResetHistory | CVarLogScale);

		CVarBool cv_denoise_shadow("pbr.denoise.shadow", "Denoiser", "Denoise sun shadows", true, CVarResetHistory);
		CVarBool cv_denoise_diffuse("pbr.denoise.diffuse", "Denoiser", "Denoise diffuse indirect lighting", true, CVarResetHistory);
		CVarBool cv_denoise_specular("pbr.denoise.specular", "Denoiser", "Denoise reflections", true, CVarResetHistory);
		CVarInt cv_denoise_shadow_iterations("pbr.denoise.shadow_iterations", "Denoiser", "A-trous iterations for shadows", 3, 1, 6, CVarResetHistory);
		CVarInt cv_denoise_diffuse_iterations("pbr.denoise.diffuse_iterations", "Denoiser", "A-trous iterations for diffuse lighting", 5, 1, 6, CVarResetHistory);
		CVarInt cv_denoise_specular_iterations("pbr.denoise.specular_iterations", "Denoiser", "A-trous iterations for reflections", 4, 1, 6, CVarResetHistory);
		CVarFloat cv_denoise_alpha("pbr.denoise.colour_blend", "Denoiser", "Minimum weight of the new sample in the temporal blend", 0.05f, 0.01f, 1.0f, CVarResetHistory | CVarLogScale);
		CVarFloat cv_denoise_alpha_moments("pbr.denoise.moments_blend", "Denoiser", "Minimum weight of the new sample in the moments blend", 0.2f, 0.01f, 1.0f, CVarResetHistory | CVarLogScale);
		CVarInt cv_denoise_history("pbr.denoise.max_history", "Denoiser", "Longest temporal history (frames)", 32, 1, 256, CVarResetHistory);
		CVarBool cv_denoise_filtered_history("pbr.denoise.filtered_history", "Denoiser", "Feed the filtered result back as history (SVGF; converges faster, but its luminance edge stopping darkens sparse bright samples, such as emissive light samples, more every frame)", false, CVarResetHistory);
		CVarFloat cv_denoise_spatial_variance("pbr.denoise.spatial_variance", "Denoiser", "Floor of the luminance variance from the 3x3 neighbourhood (scale; 0: per pixel moments only, which darkens sparse bright samples)", 1.0f, 0.0f, 16.0f, CVarResetHistory);
		CVarFloat cv_denoise_phi_colour("pbr.denoise.phi_colour", "Denoiser", "Luminance edge stopping (standard deviations)", 4.0f, 0.1f, 64.0f, CVarResetHistory | CVarLogScale);
		CVarFloat cv_denoise_phi_normal("pbr.denoise.phi_normal", "Denoiser", "Normal edge stopping exponent", 64.0f, 1.0f, 512.0f, CVarResetHistory | CVarLogScale);
		CVarFloat cv_denoise_phi_depth("pbr.denoise.phi_depth", "Denoiser", "Depth edge stopping (relative depth difference per step)", 0.02f, 0.001f, 1.0f, CVarResetHistory | CVarLogScale);

		CVarBool cv_taa("pbr.taa.enable", "Anti-aliasing", "Temporal anti-aliasing (with sub pixel jitter)", true, CVarResetHistory);
		CVarFloat cv_taa_blend("pbr.taa.blend", "Anti-aliasing", "Weight of the new frame", 0.1f, 0.01f, 1.0f, CVarResetHistory);
		CVarFloat cv_taa_gamma("pbr.taa.clip_gamma", "Anti-aliasing", "History clipping box size (standard deviations)", 1.25f, 0.5f, 4.0f, CVarResetHistory);

		CVarBool cv_auto_exposure("pbr.exposure.auto", "Exposure", "Automatic exposure from the luminance histogram", true);
		CVarFloat cv_ev100("pbr.exposure.ev100", "Exposure", "Manual exposure value (ISO 100)", 14.0f, -5.0f, 20.0f);
		CVarFloat cv_exposure_compensation("pbr.exposure.compensation", "Exposure", "Exposure compensation (stops)", 0.0f, -6.0f, 6.0f);
		CVarFloat cv_exposure_speed("pbr.exposure.adaptation_speed", "Exposure", "Automatic exposure adaptation speed (per second, 0 = instant)", 2.0f, 0.0f, 20.0f);
		CVarFloat cv_exposure_min("pbr.exposure.min_ev100", "Exposure", "Lowest automatic exposure value", -6.0f, -10.0f, 20.0f);
		CVarFloat cv_exposure_max("pbr.exposure.max_ev100", "Exposure", "Highest automatic exposure value", 17.0f, -10.0f, 24.0f);
		CVarInt cv_tonemap("pbr.tonemap.operator", "Exposure", "Tone mapping curve", 4, { "None (Clamp)", "Reinhard", "ACES (Fitted)", "AgX", "Khronos PBR Neutral" });
		CVarFloat cv_dither("pbr.tonemap.dither", "Exposure", "Dither strength (8 bit steps)", 1.0f, 0.0f, 2.0f);

		CVarInt cv_pt_bounces("pbr.pt.max_bounces", "Path Tracer", "Longest path (bounces)", 8, 1, 32, CVarResetHistory);
		CVarInt cv_pt_rr("pbr.pt.russian_roulette_start", "Path Tracer", "Bounce from which Russian roulette ends paths", 3, 1, 32, CVarResetHistory);
		CVarInt cv_pt_max_frames("pbr.pt.max_frames", "Path Tracer", "Accumulated frames after which the image stops refining", 4096, 1, 65536, CVarResetHistory);

		constexpr float kHistogramMinLog2 = -12.0f;
		constexpr float kHistogramRangeLog2 = 32.0f;
		constexpr sg::u32 kMaxTimers = 64;

		float halton(sg::u32 index, sg::u32 base)
		{
			float f = 1.0f, r = 0.0f;
			while (index > 0)
			{
				f /= static_cast<float>(base);
				r += f * static_cast<float>(index % base);
				index /= base;
			}
			return r;
		}

		// Sun transmittance through the atmosphere from the viewer (same model as pbrSky.hlsl)
		XMFLOAT3 atmosphere_transmittance(XMFLOAT3 sun, float rayleigh_scale, float mie_scale, float ozone_scale)
		{
			const XMVECTOR origin = XMVectorSet(0.0f, PBR_EARTH_RADIUS + PBR_VIEWER_ALTITUDE, 0.0f, 0.0f);
			const XMVECTOR direction = XMVector3Normalize(XMLoadFloat3(&sun));
			// Below the horizon: the Earth blocks the sun
			const float b = XMVectorGetX(XMVector3Dot(origin, direction));
			const float c_earth = XMVectorGetX(XMVector3Dot(origin, origin)) - PBR_EARTH_RADIUS * PBR_EARTH_RADIUS;
			if (b < 0.0f && b * b - c_earth > 0.0f)
			{
				return { 0.0f, 0.0f, 0.0f };
			}
			const float c_top = XMVectorGetX(XMVector3Dot(origin, origin)) - PBR_ATMOSPHERE_RADIUS * PBR_ATMOSPHERE_RADIUS;
			const float length = -b + std::sqrt(b * b - c_top);
			const int steps = 64;
			const float dt = length / steps;
			float depth[3] = {};
			for (int i = 0; i < steps; i++)
			{
				const XMVECTOR p = XMVectorAdd(origin, XMVectorScale(direction, (i + 0.5f) * dt));
				const float altitude = XMVectorGetX(XMVector3Length(p)) - PBR_EARTH_RADIUS;
				const float rayleigh = std::exp(-altitude / PBR_RAYLEIGH_SCALE_HEIGHT) * rayleigh_scale;
				const float mie = std::exp(-altitude / PBR_MIE_SCALE_HEIGHT) * mie_scale;
				const float ozone = std::max(0.0f, 1.0f - std::abs(altitude - PBR_OZONE_CENTRE) / PBR_OZONE_WIDTH) * ozone_scale;
				depth[0] += (PBR_RAYLEIGH_SCATTERING_R * rayleigh + PBR_MIE_EXTINCTION * mie + PBR_OZONE_ABSORPTION_R * ozone) * dt;
				depth[1] += (PBR_RAYLEIGH_SCATTERING_G * rayleigh + PBR_MIE_EXTINCTION * mie + PBR_OZONE_ABSORPTION_G * ozone) * dt;
				depth[2] += (PBR_RAYLEIGH_SCATTERING_B * rayleigh + PBR_MIE_EXTINCTION * mie + PBR_OZONE_ABSORPTION_B * ozone) * dt;
			}
			return { std::exp(-depth[0]), std::exp(-depth[1]), std::exp(-depth[2]) };
		}

		void store_planes(const XMMATRIX& view_projection, std::array<XMFLOAT4, 6>& planes)
		{
			// Row vector convention: clip = p * M, so planes come from M's columns (the rows of its transpose)
			const XMMATRIX t = XMMatrixTranspose(view_projection);
			const XMVECTOR c0 = t.r[0], c1 = t.r[1], c2 = t.r[2], c3 = t.r[3];
			const XMVECTOR p[6] =
			{
				XMVectorAdd(c3, c0), XMVectorSubtract(c3, c0),	// Left, right
				XMVectorAdd(c3, c1), XMVectorSubtract(c3, c1),	// Bottom, top
				XMVectorSubtract(c3, c2),						// Near (reverse Z: depth <= 1)
				c2,												// Far (reverse Z: depth >= 0; at infinity, always passes)
			};
			for (int i = 0; i < 6; i++)
			{
				const float length = XMVectorGetX(XMVector3Length(p[i]));
				XMStoreFloat4(&planes[i], length > 1e-6f ? XMVectorScale(p[i], 1.0f / length) : XMVectorSet(0, 0, 0, 1));
			}
		}
	}

	Renderer::Renderer(sg::SharedPtr<sg::Device> device_, sg::SharedPtr<sg::CommandQueue> queue_, sg::u32 width_, sg::u32 height_)
		: device(device_), queue(queue_), width(width_), height(height_)
	{
		supported = device->supports_raytracing() && device->SupportsMeshShaders();
		if (!supported)
		{
			seWriteLine("PBR: the viewer needs hardware ray tracing (DXR 1.1), mesh shaders and bindless resources (D3D12)");
			return;
		}
		scene = std::make_unique<Scene>(device);
		pass_binding_desc.cbv_binding_count = 2;
		pass_binding_desc.srv_binding_count = 16;
		pass_binding_desc.uav_binding_count = 8;
		pass_binding_desc.sampler_binding_count = 6;
		pass_binding_desc.bindless = true;
		indirect_arguments = device->create_indirect_arguments();
		timestamp_pool = device->create_gpu_timestamp_pool(queue.get(), kMaxTimers * 2);
	}

	Renderer::~Renderer()
	{
	}

	Renderer::Target Renderer::create_target(sg::u32 w, sg::u32 h, DXGI_FORMAT format, sg::ResourceUsageFlags usage, sg::u32 mip_count)
	{
		Target t;
		sg::ResourceCreateDesc desc(w, h, format, usage);
		desc.mip_count = mip_count;
		desc.optimised_clear_depth = 0.0f; // Reverse Z clears depth to 0 (the far plane)
		t.texture = sg::create_texture(*device, desc);
		t.srv = device->create_shader_resource_view(t.texture);
		if ((usage & sg::ResourceUsageFlags::UnorderedAccess) == sg::ResourceUsageFlags::UnorderedAccess)
		{
			t.uav = device->create_unordered_access_view(t.texture);
		}
		if ((usage & sg::ResourceUsageFlags::RenderTarget) == sg::ResourceUsageFlags::RenderTarget)
		{
			t.rtv = device->create_render_target_view(t.texture);
		}
		if ((usage & sg::ResourceUsageFlags::DepthStencil) == sg::ResourceUsageFlags::DepthStencil)
		{
			t.dsv = device->create_depth_stencil_view(t.texture);
		}
		t.bindless = device->allocate_bindless_descriptor(t.srv);
		return t;
	}

	Renderer::BufferResource Renderer::create_uav_buffer(sg::u32 element_size, sg::u32 element_count)
	{
		BufferResource b;
		element_count = std::max(element_count, 1u);
		b.buffer = sg::create_buffer(*device, static_cast<sg::u32>(sg::AlignUp<sg::u64>(static_cast<sg::u64>(element_size) * element_count, 256)), sg::BufferType::GeneralDataBuffer, true);
		b.uav = device->create_unordered_access_view(b.buffer, element_size, element_count);
		b.srv = device->create_shader_resource_view(b.buffer, element_size, element_count);
		return b;
	}

	void Renderer::create_resources()
	{
		using F = sg::ResourceUsageFlags;
		const F uav = F::UnorderedAccess;

		depth = create_target(width, height, DXGI_FORMAT_D32_FLOAT, F::DepthStencil);
		{
			sg::ResourceCreateDesc desc(width, height, DXGI_FORMAT_R32G32_UINT, F::RenderTarget);
			visibility.texture = sg::create_texture(*device, desc);
			visibility.srv = device->create_shader_resource_view(visibility.texture);
			visibility.rtv = device->create_render_target_view(visibility.texture);
			visibility.bindless = device->allocate_bindless_descriptor(visibility.srv);
		}
		// HZB: the largest powers of two not above the resolution, down to 1 x 1
		hzb_width = 1u << static_cast<sg::u32>(std::floor(std::log2(static_cast<float>(width))));
		hzb_height = 1u << static_cast<sg::u32>(std::floor(std::log2(static_cast<float>(height))));
		hzb_mips = static_cast<sg::u32>(std::floor(std::log2(static_cast<float>(std::max(hzb_width, hzb_height))))) + 1;
		hzb = create_target(hzb_width, hzb_height, DXGI_FORMAT_R32_FLOAT, uav, hzb_mips);
		hzb_mip_uavs.clear();
		for (sg::u32 mip = 0; mip < hzb_mips; mip++)
		{
			hzb_mip_uavs.push_back(device->create_unordered_access_view(hzb.texture, mip));
		}

		gbuffer_albedo = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
		gbuffer_normal = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
		gbuffer_geometry = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
		gbuffer_emissive = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
		motion = create_target(width, height, DXGI_FORMAT_R16G16_FLOAT, uav);
		guide[0] = create_target(width, height, DXGI_FORMAT_R32G32_UINT, uav);
		guide[1] = create_target(width, height, DXGI_FORMAT_R32G32_UINT, uav);
		shadow_raw = create_target(width, height, DXGI_FORMAT_R16_FLOAT, uav);
		diffuse_raw = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
		specular_raw = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
		for (DenoiseTargets* d : { &shadow_denoise, &diffuse_denoise, &specular_denoise })
		{
			d->accumulated = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
			d->moments[0] = create_target(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, uav);
			d->moments[1] = create_target(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, uav);
			d->history[0] = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
			d->history[1] = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
			d->ping = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
			d->pong = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
		}
		lit = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
		taa_history[0] = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
		taa_history[1] = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
		path_accumulation = create_target(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, uav);
		path_output = create_target(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
		sky = create_target(sky_width, sky_height, DXGI_FORMAT_R16G16B16A16_FLOAT, uav);
		sky_multiple_scattering = create_target(32, 32, DXGI_FORMAT_R32G32B32A32_FLOAT, uav);
		dummy_texture = create_target(1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, F::None);

		task_counts = create_uav_buffer(4, 4);
		draw_arguments = create_uav_buffer(16, PBR_BUCKET_COUNT);
		cull_statistics = create_uav_buffer(4, PBR_STAT_COUNT);
		histogram = create_uav_buffer(4, 256);
		cache_keys = create_uav_buffer(4, cache_capacity);
		cache_accumulators = create_uav_buffer(16, cache_capacity);
		cache_values = create_uav_buffer(16, cache_capacity);
		dummy_srv_buffer = create_uav_buffer(16, 1);
		dummy_uav_buffer = create_uav_buffer(16, 1);
		statistics_readback = sg::create_readback_buffer(*device, 256);
		histogram_readback = sg::create_readback_buffer(*device, 1024);
		resources_created = true;
	}

	sg::SharedPtr<sg::Pipeline> Renderer::compute_pipeline(const char* shader)
	{
		sg::PipelineDesc::Compute desc;
		desc.compute_shader = sg::create_compute_shader(*device, get_shader_path(shader));
		sg::SharedPtr<sg::Pipeline> pipeline = device->create_pipeline(desc, pass_binding_desc);
		seAssert(pipeline != nullptr, "Failed to create %s", shader);
		return pipeline;
	}

	void Renderer::create_pipelines()
	{
		pipelines.cull_instances = compute_pipeline("PBR_CullInstances");
		pipelines.build_draw_arguments = compute_pipeline("PBR_BuildDrawArguments");
		pipelines.hzb_first = compute_pipeline("PBR_BuildHZBFirst");
		pipelines.hzb_down = compute_pipeline("PBR_BuildHZBDown");
		pipelines.resolve = compute_pipeline("PBR_Resolve");
		pipelines.sky = compute_pipeline("PBR_Sky");
		pipelines.sky_multiple_scattering = compute_pipeline("PBR_SkyMultipleScattering");
		pipelines.lighting = compute_pipeline("PBR_Lighting");
		pipelines.cache_resolve = compute_pipeline("PBR_RadianceCacheResolve");
		pipelines.denoise_temporal = compute_pipeline("PBR_DenoiseTemporal");
		pipelines.denoise_variance = compute_pipeline("PBR_DenoiseVariance");
		pipelines.denoise_atrous = compute_pipeline("PBR_DenoiseAtrous");
		pipelines.composite = compute_pipeline("PBR_Composite");
		pipelines.taa = compute_pipeline("PBR_TAA");
		pipelines.histogram = compute_pipeline("PBR_Histogram");
		pipelines.tonemap = compute_pipeline("PBR_Tonemap");
		pipelines.path_trace = compute_pipeline("PBR_PathTrace");

		for (sg::u32 bucket = 0; bucket < PBR_BUCKET_COUNT; bucket++)
		{
			const bool masked = bucket == PBR_BUCKET_MASKED;
			sg::PipelineDesc::Mesh desc;
			desc.amp_shader = sg::create_amplification_shader(*device, get_shader_path("PBR_Amplification"));
			desc.mesh_shader = sg::create_mesh_shader(*device, get_shader_path(masked ? "PBR_MeshMasked" : "PBR_Mesh"));
			desc.pixel_shader = sg::create_pixel_shader(*device, get_shader_path(masked ? "PBR_VisibilityMasked" : "PBR_Visibility"));
			desc.render_target_count = 1;
			desc.render_target_format_list[0] = DXGI_FORMAT_R32G32_UINT;
			desc.depth_stencil_format = DXGI_FORMAT_D32_FLOAT;
			desc.depth_stencil_desc.depth_enable = true;
			desc.depth_stencil_desc.depth_write = true;
			desc.depth_stencil_desc.comparison_function = sg::ComparisonFunction::Greater; // Reverse Z
			desc.rasterizer_desc.cull_mode = masked ? sg::Rasterizer::CullMode::None : sg::Rasterizer::CullMode::Back;
			pipelines.visibility[bucket] = device->create_pipeline(desc, pass_binding_desc);
			seAssert(pipelines.visibility[bucket] != nullptr, "Failed to create the visibility pipeline");
		}
	}

	bool Renderer::load_scene(const std::string& name_or_path, Camera* camera)
	{
		if (!supported)
		{
			return false;
		}
		const ScenePreset* preset = nullptr;
		for (const ScenePreset& p : scene_presets())
		{
			if (name_or_path == p.name)
			{
				preset = &p;
			}
		}
		const std::string path = preset ? preset->path : name_or_path;
		const bool loaded = scene->get_path() == path && scene->get_state() != Scene::State::Failed && scene->get_state() != Scene::State::Empty;
		if (loaded)
		{
			// Another preset of the same file: keep the scene
			scene->set_preset(preset);
			clear_radiance_cache = true;
			if (camera && scene->is_ready())
			{
				apply_preset_camera(*camera);
			}
			else
			{
				camera_on_ready = camera;
			}
		}
		else
		{
			scene->load(path, preset);
			// The camera moves to the preset's view once the scene is ready (file cameras are known after the import)
			camera_on_ready = camera;
		}
		if (preset)
		{
			cv_sun_elevation.set(preset->sun_elevation);
			cv_sun_azimuth.set(preset->sun_azimuth);
		}
		update_camera_track();
		return true;
	}

	bool Renderer::preset_camera(XMFLOAT3& position, XMFLOAT3& target) const
	{
		if (!scene || !scene->get_preset())
		{
			return false;
		}
		const ScenePreset& preset = *scene->get_preset();
		const bool from_file = preset.camera_position.x == preset.camera_target.x && preset.camera_position.y == preset.camera_target.y && preset.camera_position.z == preset.camera_target.z;
		if (from_file)
		{
			return scene->get_file_camera(position, target);
		}
		position = preset.camera_position;
		target = preset.camera_target;
		return true;
	}

	void Renderer::apply_preset_camera(Camera& camera) const
	{
		XMFLOAT3 position, target;
		if (preset_camera(position, target))
		{
			camera.SetLookAt(position, target);
		}
	}

	bool Renderer::apply_test_settings(const nlohmann::json& settings, Camera& camera, std::string& error)
	{
		CVars::reset_all("pbr.");
		if (settings.contains("scene"))
		{
			const std::string name = settings["scene"].get<std::string>();
			const bool same = scene->get_preset() ? name == scene->get_preset()->name : name == scene->get_path();
			if (!same || scene->get_state() == Scene::State::Failed)
			{
				load_scene(name, &camera);
			}
			else if (scene->get_preset())
			{
				cv_sun_elevation.set(scene->get_preset()->sun_elevation);
				cv_sun_azimuth.set(scene->get_preset()->sun_azimuth);
			}
		}
		if (settings.contains("settings") && !CVars::apply_json(settings["settings"], error))
		{
			return false;
		}
		history_valid = false;
		accumulated_frames = 0;
		exposure_initialised = false;
		clear_radiance_cache = true;
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------------

	void Renderer::update(float delta_time, Camera& camera)
	{
		if (!supported)
		{
			return;
		}
		// Results of the previous frame (the application waits for each frame to finish)
		if (frame_recorded)
		{
			timestamp_pool->resolve();
			// Averaged over recent frames (single frame GPU timings are noisy)
			std::vector<std::pair<const char*, double>> previous = std::move(timings);
			timings.clear();
			for (const Timer& t : timers)
			{
				double us = timestamp_pool->collect_timestamp_us(t.index);
				for (const auto& [name, average] : previous)
				{
					if (strcmp(name, t.name) == 0)
					{
						us = average + (us - average) * 0.1;
					}
				}
				timings.push_back({ t.name, us });
			}
			if (readback_valid)
			{
				statistics_readback->read_memory(0, cull_stats.data(), sizeof(sg::u32) * PBR_STAT_COUNT);
			}
			update_exposure(delta_time);
			frame_recorded = false;
		}
		update_camera_track();
		imgui(camera);
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// Camera tracks

	void Renderer::update_camera_track()
	{
		if (!scene)
		{
			return;
		}
		const ScenePreset* preset = scene->get_preset();
		const bool changed = preset != camera_track_preset || scene->get_path() != camera_track_path;
		if (!changed && !(camera_track_needs_bounds && scene->is_ready()))
		{
			return;
		}
		if (changed)
		{
			camera_track_preset = preset;
			camera_track_path = scene->get_path();
			camera_track = gdr::CameraTrack();
			camera_track_needs_bounds = true;
			camera_track_message.clear();
			if (preset && preset->camera_track)
			{
				if (gdr::load_camera_track(preset->camera_track, camera_track) && camera_track.is_valid())
				{
					camera_track_needs_bounds = false;
				}
				else
				{
					camera_track = gdr::CameraTrack();
					camera_track_message = std::string("Could not load ") + preset->camera_track + ": orbiting the scene instead";
				}
			}
			camera_track_version++;
		}
		if (camera_track_needs_bounds && scene->is_ready())
		{
			// An orbit around the scene: eight points on a circle outside its bounds, rising and falling, looking at the centre
			const XMFLOAT3 lo = scene->get_bounds_min();
			const XMFLOAT3 hi = scene->get_bounds_max();
			const XMFLOAT3 centre = { (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };
			const float extent_x = hi.x - lo.x, extent_y = hi.y - lo.y, extent_z = hi.z - lo.z;
			const float radius = std::max(0.8f * std::sqrt(extent_x * extent_x + extent_z * extent_z) + 0.25f * extent_y, 0.5f);
			camera_track = gdr::CameraTrack();
			camera_track.duration = 24.0f;
			camera_track.near_plane = std::max(radius * 0.005f, 0.005f);
			camera_track.far_plane = radius * 50.0f;
			for (int i = 0; i < 8; i++)
			{
				const float angle = XM_2PI * i / 8.0f;
				gdr::CameraTrackPoint point;
				point.position = { centre.x + radius * std::sin(angle), centre.y + extent_y * (0.15f + 0.25f * std::sin(angle * 2.0f)), centre.z - radius * std::cos(angle) };
				point.target = { centre.x, centre.y + extent_y * 0.05f * std::cos(angle * 3.0f), centre.z };
				camera_track.points.push_back(point);
			}
			camera_track_needs_bounds = false;
			camera_track_version++;
		}
	}

	void Renderer::camera_track_imgui(Camera& camera)
	{
		if (!ImGui::CollapsingHeader("Camera Track"))
		{
			return;
		}
		ImGui::PushID("PBRCameraTrack");
		const char* file = camera_track_preset && camera_track_preset->camera_track ? camera_track_preset->camera_track : "PBRViewer/CameraTracks/Custom.json";
		ImGui::TextWrapped("%zu points, %.1f s loop. Play it from the Slim Graphics window's Camera Track header. File: %s", camera_track.points.size(), camera_track.duration, file);
		bool edited = false;
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragFloat("Target distance", &camera_track_target_distance, 0.1f, 0.1f, 100.0f, "%.1f m");
		if (ImGui::Button("Add point at camera"))
		{
			// The view matrix's eye and direction (Camera::GetPosition is ahead of the matrix while the camera moves)
			const ShaderStructs::CameraData& data = camera.GetCameraShaderData();
			gdr::CameraTrackPoint point;
			point.position = { data.camera_position.x, data.camera_position.y, data.camera_position.z };
			point.target = { data.camera_position.x + data.camera_direction.x * camera_track_target_distance, data.camera_position.y + data.camera_direction.y * camera_track_target_distance,
				data.camera_position.z + data.camera_direction.z * camera_track_target_distance };
			if (camera_track.points.empty())
			{
				camera_track.duration = std::max(camera_track.duration, 30.0f);
				camera_track.near_plane = camera.GetNearPlane();
				camera_track.far_plane = std::max(camera_track.far_plane, 1000.0f);
			}
			camera_track.points.push_back(point);
			edited = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove last") && !camera_track.points.empty())
		{
			camera_track.points.pop_back();
			edited = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
		{
			camera_track.points.clear();
			edited = true;
		}
		ImGui::SetNextItemWidth(120.0f);
		edited |= ImGui::DragFloat("Duration", &camera_track.duration, 0.5f, 1.0f, 600.0f, "%.1f s");
		ImGui::SetNextItemWidth(120.0f);
		edited |= ImGui::DragFloat("Near plane", &camera_track.near_plane, 0.001f, 0.001f, 10.0f, "%.3f m");
		ImGui::SetNextItemWidth(120.0f);
		edited |= ImGui::DragFloat("Far plane", &camera_track.far_plane, 1.0f, 1.0f, 100000.0f, "%.0f m");
		if (ImGui::Button("Save"))
		{
			camera_track_message = gdr::save_camera_track(camera_track, file) ? std::string("Saved ") + file : std::string("Could not write ") + file;
		}
		ImGui::SameLine();
		if (ImGui::Button("Reload"))
		{
			camera_track_preset = nullptr;
			camera_track_path.clear();
			update_camera_track();
		}
		if (!camera_track.is_valid())
		{
			ImGui::TextDisabled("A track needs at least 3 points");
		}
		if (!camera_track_message.empty())
		{
			ImGui::TextWrapped("%s", camera_track_message.c_str());
		}
		if (edited)
		{
			camera_track_version++;
		}
		ImGui::PopID();
	}

	void Renderer::update_exposure(float delta_time)
	{
		float target_ev100 = cv_ev100;
		if (cv_auto_exposure && readback_valid)
		{
			std::array<sg::u32, 256> bins = {};
			histogram_readback->read_memory(0, bins.data(), sizeof(bins));
			// Average log2 luminance of the pixels between the 40th and 95th percentiles (ignoring black pixels)
			sg::u64 total = 0;
			for (sg::u32 i = 1; i < 256; i++) total += bins[i];
			if (total > 0)
			{
				const double low = total * 0.4, high = total * 0.95;
				double count = 0.0, sum = 0.0, weight = 0.0;
				for (sg::u32 i = 1; i < 256; i++)
				{
					const double lo = count, hi = count + bins[i];
					count = hi;
					const double inside = std::max(0.0, std::min(hi, high) - std::max(lo, low));
					if (inside <= 0.0) continue;
					const double log2_l = kHistogramMinLog2 + (i - 1 + 0.5) / 254.0 * kHistogramRangeLog2;
					sum += log2_l * inside;
					weight += inside;
				}
				if (weight > 0.0)
				{
					// EV100 = log2(L * S / K), S = 100, K = 12.5. The histogram holds pre-exposure luminance.
					const double average = sum / weight;
					target_ev100 = static_cast<float>(average + std::log2(100.0 / 12.5));
					target_ev100 = std::clamp(target_ev100, cv_exposure_min.get(), cv_exposure_max.get());
				}
			}
		}
		if (!exposure_initialised || !cv_auto_exposure || cv_exposure_speed <= 0.0f)
		{
			exposure_ev100 = target_ev100;
			exposure_initialised = cv_auto_exposure ? readback_valid : true;
		}
		else
		{
			const float t = 1.0f - std::exp(-delta_time * cv_exposure_speed);
			exposure_ev100 += (target_ev100 - exposure_ev100) * t;
		}
		// Photometric exposure of a camera with this EV100 (saturation based: 1.2 * 2^EV100 maps to white)
		exposure = std::exp2(cv_exposure_compensation.get()) / (1.2f * std::exp2(exposure_ev100));
	}

	void Renderer::imgui(Camera& camera)
	{
		CVars::imgui_window("PBR Settings", "pbr.");
		if (ImGui::Begin("PBR Viewer"))
		{
			static int selected = 0;
			const std::vector<ScenePreset>& presets = scene_presets();
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::BeginCombo("##scene", presets[selected].name))
			{
				for (int i = 0; i < (int)presets.size(); i++)
				{
					if (ImGui::Selectable(presets[i].name, i == selected)) selected = i;
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::Button("Load"))
			{
				pending_preset_load = presets[selected].name;
			}
			ImGui::Text("Scene: %s", scene->get_status().c_str());
			if (scene->is_ready())
			{
				const Scene::Statistics& s = scene->get_statistics();
				ImGui::Text("%u instances, %u meshes, %u meshlets, %u triangles", s.instances, s.meshes, s.meshlets, s.triangles);
				ImGui::Text("%u materials (%u alpha tested), %u textures (%.0f MiB), BVH %.0f MiB", s.materials, s.alpha_tested_materials, s.textures, s.texture_bytes / 1048576.0, s.acceleration_structure_bytes / 1048576.0);
				ImGui::Text("Import: %.1f s", s.import_seconds);
			}
			if (ImGui::CollapsingHeader("Culling", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Instances culled: frustum %u, occlusion %u", cull_stats[PBR_STAT_INSTANCES_FRUSTUM_CULLED], cull_stats[PBR_STAT_INSTANCES_OCCLUSION_CULLED]);
				ImGui::Text("Meshlets culled: frustum %u, cone %u, occlusion %u", cull_stats[PBR_STAT_MESHLETS_FRUSTUM_CULLED], cull_stats[PBR_STAT_MESHLETS_CONE_CULLED], cull_stats[PBR_STAT_MESHLETS_OCCLUSION_CULLED]);
				ImGui::Text("Meshlets drawn: early %u, late %u (%u triangles)", cull_stats[PBR_STAT_MESHLETS_DRAWN_EARLY], cull_stats[PBR_STAT_MESHLETS_DRAWN_LATE], cull_stats[PBR_STAT_TRIANGLES_DRAWN]);
			}
			if (ImGui::CollapsingHeader("GPU Timings", ImGuiTreeNodeFlags_DefaultOpen))
			{
				double total = 0.0;
				for (const auto& [name, us] : timings)
				{
					ImGui::Text("%-24s %8.3f ms", name, us / 1000.0);
					total += us;
				}
				ImGui::Text("%-24s %8.3f ms", "Total", total / 1000.0);
			}
			camera_track_imgui(camera);
			ImGui::Text("Exposure: EV100 %.2f", exposure_ev100);
			if (cv_mode == 1)
			{
				ImGui::Text("Path tracer: %u / %d frames", accumulated_frames, cv_pt_max_frames.get());
			}
		}
		ImGui::End();
	}

	double Renderer::gpu_milliseconds() const
	{
		double total = 0.0;
		for (const auto& [name, us] : timings)
		{
			total += us;
		}
		return total / 1000.0;
	}

	std::string Renderer::get_statistics_line() const
	{
		char text[640];
		const Scene::Statistics& s = scene ? scene->get_statistics() : Scene::Statistics();
		sprintf_s(text, "pbr_instances=%u pbr_meshlets=%u pbr_triangles=%u pbr_materials=%u pbr_alpha_tested=%u pbr_emissive_triangles=%u pbr_textures=%u pbr_meshlets_drawn=%u pbr_triangles_drawn=%u pbr_meshlets_occluded=%u pbr_accumulated=%u pbr_ev100=%.2f pbr_gpu_ms=%.2f",
			s.instances, s.meshlets, s.triangles, s.materials, s.alpha_tested_materials, s.emissive_triangles, s.textures,
			cull_stats[PBR_STAT_MESHLETS_DRAWN_EARLY] + cull_stats[PBR_STAT_MESHLETS_DRAWN_LATE], cull_stats[PBR_STAT_TRIANGLES_DRAWN],
			cull_stats[PBR_STAT_MESHLETS_OCCLUSION_CULLED], accumulated_frames, exposure_ev100, gpu_milliseconds());
		// Pass timings (not compared between runs: they depend on the GPU)
		std::string line = text;
		for (const auto& [name, us] : timings)
		{
			std::string key = name;
			std::transform(key.begin(), key.end(), key.begin(), [](char c) { return std::isalnum((unsigned char)c) ? (char)std::tolower((unsigned char)c) : '_'; });
			char value[64];
			sprintf_s(value, " pbr_ms_%s=%.2f", key.c_str(), us / 1000.0);
			line += value;
		}
		return line;
	}

	// ---------------------------------------------------------------------------------------------------------------------

	void Renderer::update_sun()
	{
		const float elevation = XMConvertToRadians(cv_sun_elevation);
		const float azimuth = XMConvertToRadians(cv_sun_azimuth);
		const XMFLOAT3 sun = { std::cos(elevation) * std::cos(azimuth), std::sin(elevation), std::cos(elevation) * std::sin(azimuth) };
		const float radius = XMConvertToRadians(cv_sun_radius);
		sun_transmittance = atmosphere_transmittance(sun, cv_sky_rayleigh, cv_sky_mie, cv_sky_ozone);
		const float e = cv_sun_illuminance;
		frame.sun_direction = { sun.x, sun.y, sun.z, std::cos(radius) };
		frame.sun_illuminance = { e * sun_transmittance.x, e * sun_transmittance.y, e * sun_transmittance.z, radius };
		// Radiance of a uniform disk with that illuminance: E / solid angle
		const float solid_angle = 2.0f * XM_PI * (1.0f - std::cos(radius));
		frame.sun_disk_radiance = { frame.sun_illuminance.x / solid_angle, frame.sun_illuminance.y / solid_angle, frame.sun_illuminance.z / solid_angle, 0.0f };
		if (cv_debug_view == PBR_DEBUG_WHITE_FURNACE)
		{
			// Lit by the uniform sky alone
			frame.sun_illuminance = { 0.0f, 0.0f, 0.0f, radius };
			frame.sun_disk_radiance = { 0.0f, 0.0f, 0.0f, 0.0f };
		}
	}

	void Renderer::update_frame_constants(const Camera& camera)
	{
		const ShaderStructs::CameraData& camera_data = camera.GetCameraShaderData();
		const XMMATRIX view = camera_data.view_matrix;
		const float ys = XMVectorGetY(camera_data.projection_matrix.r[1]);	// 1 / tan(fov / 2)
		const float xs = ys * static_cast<float>(height) / static_cast<float>(width);
		const float near_plane = camera.GetNearPlane();

		// Sub pixel jitter (Halton 2, 3) for temporal anti-aliasing; none for the path tracer (it jitters per sample)
		const bool data_view = cv_debug_view != PBR_DEBUG_NONE && cv_debug_view != PBR_DEBUG_DIRECT && cv_debug_view != PBR_DEBUG_INDIRECT &&
			cv_debug_view != PBR_DEBUG_DIFFUSE && cv_debug_view != PBR_DEBUG_SPECULAR && cv_debug_view != PBR_DEBUG_DIFFUSE_RAW &&
			cv_debug_view != PBR_DEBUG_SPECULAR_RAW && cv_debug_view != PBR_DEBUG_EMISSIVE && cv_debug_view != PBR_DEBUG_WHITE_FURNACE && cv_debug_view != PBR_DEBUG_RADIANCE_CACHE;
		XMFLOAT2 jitter = { 0.0f, 0.0f };
		if (cv_taa && cv_mode == 0 && !data_view)
		{
			const sg::u32 i = (frame_index % 16) + 1;
			jitter = { halton(i, 2) - 0.5f, halton(i, 3) - 0.5f };
		}
		auto projection = [&](XMFLOAT2 j)
			{
				// Left handed, reverse Z, infinite far plane: depth = near / z
				return XMMATRIX(
					xs, 0.0f, 0.0f, 0.0f,
					0.0f, ys, 0.0f, 0.0f,
					2.0f * j.x / width, -2.0f * j.y / height, 0.0f, 1.0f,
					0.0f, 0.0f, near_plane, 0.0f);
			};
		const XMMATRIX proj = projection(jitter);
		const XMMATRIX proj_no_jitter = projection({ 0.0f, 0.0f });
		const XMMATRIX view_proj = XMMatrixMultiply(view, proj);
		const XMMATRIX view_proj_no_jitter = XMMatrixMultiply(view, proj_no_jitter);
		// The eye of the view matrix: Camera::Update moves its position after building the matrix, so GetPosition() is a
		// step ahead while the camera moves, and camera rays from it would not match the rasterised frame (textures slid)
		XMFLOAT3A camera_position;
		XMStoreFloat3A(&camera_position, XMMatrixInverse(nullptr, view).r[3]);

		XMStoreFloat4x4(&frame.view, view);
		XMStoreFloat4x4(&frame.projection, proj);
		XMStoreFloat4x4(&frame.view_projection, view_proj);
		XMStoreFloat4x4(&frame.view_projection_no_jitter, view_proj_no_jitter);
		XMStoreFloat4x4(&frame.inverse_view_projection, XMMatrixInverse(nullptr, view_proj));
		frame.previous_view_projection_no_jitter = history_valid ? previous_view_projection : frame.view_projection_no_jitter;
		frame.camera_position = { camera_position.x, camera_position.y, camera_position.z, near_plane };
		frame.previous_camera_position = { previous_camera_position.x, previous_camera_position.y, previous_camera_position.z, near_plane };
		if (!cv_cull_freeze || frame_index == 0)
		{
			XMStoreFloat4x4(&culling_view_projection, view_proj_no_jitter);
			culling_camera_position = { camera_position.x, camera_position.y, camera_position.z, near_plane };
			store_planes(view_proj_no_jitter, culling_planes);
		}
		frame.culling_view_projection = culling_view_projection;
		frame.culling_camera_position = culling_camera_position;
		for (int i = 0; i < 6; i++) frame.frustum_planes[i] = culling_planes[i];
		frame.resolution = { static_cast<float>(width), static_cast<float>(height) };
		frame.inverse_resolution = { 1.0f / width, 1.0f / height };
		frame.jitter = jitter;
		frame.previous_jitter = previous_jitter;

		update_sun();

		frame.frame_index = frame_index;
		sg::u32 flags = 0;
		flags |= cv_cull_frustum ? PBR_FLAG_FRUSTUM_CULLING : 0;
		flags |= cv_cull_cone ? PBR_FLAG_CONE_CULLING : 0;
		flags |= cv_cull_occlusion ? PBR_FLAG_OCCLUSION_CULLING : 0;
		flags |= cv_shadows ? PBR_FLAG_SHADOWS : 0;
		flags |= cv_diffuse_gi ? PBR_FLAG_DIFFUSE_GI : 0;
		flags |= cv_specular ? PBR_FLAG_SPECULAR : 0;
		flags |= cv_radiance_cache ? PBR_FLAG_RADIANCE_CACHE : 0;
		flags |= cv_sky_lighting ? PBR_FLAG_SKY_LIGHTING : 0;
		flags |= cv_emissive ? PBR_FLAG_EMISSIVE : 0;
		flags |= cv_textures ? PBR_FLAG_TEXTURES : 0;
		flags |= cv_normal_maps ? PBR_FLAG_NORMAL_MAPS : 0;
		flags |= cv_alpha_test ? PBR_FLAG_ALPHA_TEST : 0;
		flags |= cv_sun_disk ? PBR_FLAG_SUN_DISK : 0;
		frame.flags = flags;
		frame.debug_view = static_cast<sg::u32>(cv_debug_view.get());
		frame.hzb_mip_count = hzb_mips;
		frame.hzb_size = { static_cast<float>(hzb_width), static_cast<float>(hzb_height) };
		frame.exposure = exposure;
		frame.emissive_scale = cv_emissive_scale;
		frame.roughness_min = cv_roughness_min;
		frame.roughness_override = cv_roughness_override;
		frame.metallic_override = cv_metallic_override;
		frame.texture_lod_bias = cv_lod_bias;
		frame.albedo_override = cv_albedo_override_enable ? XMFLOAT3(cv_albedo_override[0], cv_albedo_override[1], cv_albedo_override[2]) : XMFLOAT3(-1.0f, -1.0f, -1.0f);
		frame.sky_intensity = cv_sky_intensity;
		frame.ray_offset = cv_ray_offset;
		frame.max_ray_distance = cv_max_distance;
		frame.firefly_clamp = cv_firefly;
		frame.scene_scale = 1.0f;
		frame.radiance_cache = { cv_cache_cell, cv_cache_min_cell, cv_cache_blend, static_cast<float>(cache_capacity) };
		frame.sky_parameters = { cv_sky_ground, static_cast<float>(sky_width), static_cast<float>(sky_height), cv_alpha_lod_bias };
		frame.alpha_test_limits = { static_cast<sg::u32>(cv_alpha_tests_primary.get()), static_cast<sg::u32>(cv_alpha_tests_secondary.get()), 0, 0 };
		const bool emissive_ready = scene && scene->is_ready();
		frame.emissive_lights = { emissive_ready ? static_cast<float>(scene->get_emissive_triangle_count()) : 0.0f, emissive_ready ? scene->get_emissive_weight() : 0.0f,
			cv_emissive_sampling ? 1.0f : 0.0f, static_cast<float>(cv_emissive_candidates.get()) };

		// History for next frame
		XMStoreFloat4x4(&previous_view_projection, view_proj_no_jitter);
		previous_camera_position = { camera_position.x, camera_position.y, camera_position.z };
		previous_jitter = jitter;

		// The path tracer restarts when the view or a setting that changes the image changes
		XMFLOAT4X4 camera_matrix;
		XMStoreFloat4x4(&camera_matrix, view_proj_no_jitter);
		const sg::u32 reset_count = CVars::get_change_count(CVarResetHistory);
		if (reset_count != cache_reset_count)
		{
			cache_reset_count = reset_count;
			clear_radiance_cache = true;
		}
		if (memcmp(&camera_matrix, &last_camera_matrix, sizeof(camera_matrix)) != 0 || reset_count != last_reset_count || seen_scene_version != scene_version)
		{
			accumulated_frames = 0;
			last_camera_matrix = camera_matrix;
			last_reset_count = reset_count;
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------

	void Renderer::bind(sg::CommandList& command_list, sg::PipelineType type, sg::ConstantBufferView& pass_cbv, const Binding& binding)
	{
		sg::Binding b;
		b.cbv_binding_count = pass_binding_desc.cbv_binding_count;
		b.srv_binding_count = pass_binding_desc.srv_binding_count;
		b.uav_binding_count = pass_binding_desc.uav_binding_count;
		b.sampler_binding_count = pass_binding_desc.sampler_binding_count;
		b.set_cbv(frame_cbv, 0);
		b.set_cbv(pass_cbv, 1);

		const bool ready = scene && scene->is_ready();
		Scene::Views* views = ready ? &scene->get_views() : nullptr;
		sg::ShaderResourceView* scene_srvs[9] =
		{
			views ? &views->vertices : &dummy_srv_buffer.srv,
			views ? &views->indices : &dummy_srv_buffer.srv,
			views ? &views->meshlets : &dummy_srv_buffer.srv,
			views ? &views->meshlet_vertices : &dummy_srv_buffer.srv,
			views ? &views->meshlet_triangles : &dummy_srv_buffer.srv,
			views ? &views->meshes : &dummy_srv_buffer.srv,
			views ? &views->instances : &dummy_srv_buffer.srv,
			views ? &views->materials : &dummy_srv_buffer.srv,
			views ? &views->tlas : &dummy_srv_buffer.srv,
		};
		for (sg::u32 i = 0; i < 9; i++)
		{
			b.set_srv(*scene_srvs[i], i);
		}
		b.set_srv(sky.srv, 9);
		for (sg::u32 i = 0; i < 6; i++)
		{
			b.set_srv(binding.inputs[i] ? *binding.inputs[i] : dummy_texture.srv, 10 + i);
		}
		for (sg::u32 i = 0; i < 8; i++)
		{
			b.set_uav(binding.outputs[i] ? *binding.outputs[i] : dummy_uav_buffer.uav, i);
		}
		command_list.bind(b, type);
	}

	void Renderer::dispatch_screen(sg::CommandList& command_list, sg::Pipeline* pipeline, sg::ConstantBufferView& pass_cbv, const Binding& binding)
	{
		command_list.set_pipeline(pipeline);
		bind(command_list, sg::PipelineType::Compute, pass_cbv, binding);
		command_list.dispatch((width + 7) / 8, (height + 7) / 8, 1);
	}

	void Renderer::begin_timer(sg::CommandList& command_list, const char* name)
	{
		if (timers.size() >= kMaxTimers)
		{
			return;
		}
		Timer t;
		t.name = name;
		t.index = timestamp_pool->allocate_new_timestamp();
		timestamp_pool->begin_timestamp(t.index, &command_list);
		timers.push_back(t);
		open_timer = static_cast<sg::u32>(timers.size() - 1);
	}

	void Renderer::end_timer(sg::CommandList& command_list)
	{
		if (open_timer != ~0u)
		{
			timestamp_pool->end_timestamp(timers[open_timer].index, &command_list);
			open_timer = ~0u;
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------

	void Renderer::render_sky(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer)
	{
		gpu::PassConstants pc = {};
		const float e = cv_sun_illuminance;
		pc.f0 = { e, e, e, cv_sky_mie_g };
		pc.f1 = { cv_sky_mie, cv_sky_ozone, cv_sky_rayleigh, cv_sky_ground };
		pc.u0 = { sky_width, sky_height, 0, 0 };
		sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(pc);
		// The sky texture is bound as t9 for every pass; these passes bind dummies there while writing it
		auto sky_binding = [&](sg::ShaderResourceView* input, sg::UnorderedAccessView* output, sg::u32 output_slot)
			{
				sg::Binding binding;
				binding.cbv_binding_count = 2;
				binding.srv_binding_count = 16;
				binding.uav_binding_count = 8;
				binding.sampler_binding_count = 6;
				binding.set_cbv(frame_cbv, 0);
				binding.set_cbv(cbv, 1);
				for (sg::u32 i = 0; i < 16; i++) binding.set_srv(i < 9 ? dummy_srv_buffer.srv : dummy_texture.srv, i);
				if (input) binding.set_srv(*input, 10);
				for (sg::u32 i = 0; i < 8; i++) binding.set_uav(dummy_uav_buffer.uav, i);
				binding.set_uav(*output, output_slot);
				command_list.bind(binding, sg::PipelineType::Compute);
			};
		// Higher order scattering LUT, then the sky
		command_list.set_pipeline(pipelines.sky_multiple_scattering.get());
		sky_binding(nullptr, &sky_multiple_scattering.uav, 1);
		command_list.dispatch(32 / 8, 32 / 8, 1);
		command_list.set_pipeline(pipelines.sky.get());
		sky_binding(&sky_multiple_scattering.srv, &sky.uav, 0);
		command_list.dispatch((sky_width + 7) / 8, (sky_height + 7) / 8, 1);
	}

	void Renderer::render_geometry_phase(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer, sg::u32 phase)
	{
		sg::ShaderResourceView no_srv = {};
		command_list.clear_buffer_uint(task_counts.uav, no_srv, 0);

		gpu::GeometryPassConstants gc = {};
		gc.phase = phase;
		gc.instance_count = scene->get_instance_count();
		gc.task_region_offsets = { task_region_offsets[0], task_region_offsets[1], task_region_offsets[2], task_region_offsets[3] };
		gc.task_region_sizes = { task_region_sizes[0], task_region_sizes[1], task_region_sizes[2], task_region_sizes[3] };

		Binding b;
		b.inputs[0] = &hzb.srv;
		b.outputs[0] = &tasks.uav;
		b.outputs[1] = &task_counts.uav;
		b.outputs[2] = &draw_arguments.uav;
		b.outputs[3] = &instance_visibility.uav;
		b.outputs[4] = &meshlet_visibility.uav;
		b.outputs[5] = &cull_statistics.uav;

		sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(gc);
		command_list.set_pipeline(pipelines.cull_instances.get());
		bind(command_list, sg::PipelineType::Compute, cbv, b);
		command_list.dispatch((gc.instance_count + 63) / 64, 1, 1);
		// Changing pipelines changes the root signature, which invalidates bindings
		command_list.set_pipeline(pipelines.build_draw_arguments.get());
		bind(command_list, sg::PipelineType::Compute, cbv, b);
		command_list.dispatch(1, 1, 1);

		sg::Viewport viewport;
		viewport.width = static_cast<float>(width);
		viewport.height = static_cast<float>(height);
		sg::ScissorRect scissor;
		scissor.right = width;
		scissor.bottom = height;
		command_list.start_geometry_pass(1, &visibility.rtv, viewport, scissor, depth.dsv);
		if (phase == PBR_PHASE_EARLY)
		{
			command_list.clear_render_target_view(visibility.rtv, { 0.0f, 0.0f, 0.0f, 0.0f });
		}
		indirect_arguments->set_indirect_buffer(draw_arguments.buffer);
		for (sg::u32 bucket = 0; bucket < PBR_BUCKET_COUNT; bucket++)
		{
			if (task_region_sizes[bucket] == 0)
			{
				continue;
			}
			gc.bucket = bucket;
			sg::ConstantBufferView bucket_cbv = cbuffer.AllocateAndWrite(gc);
			command_list.set_pipeline(pipelines.visibility[bucket].get());
			bind(command_list, sg::PipelineType::Geometry, bucket_cbv, b);
			command_list.execute_indirect(*indirect_arguments, bucket * 16);
		}
		command_list.end_geometry_pass();
	}

	void Renderer::build_hzb(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer)
	{
		for (sg::u32 mip = 0; mip < hzb_mips; mip++)
		{
			gpu::PassConstants pc = {};
			gpu::GeometryPassConstants gc = {};
			gc.phase = mip;
			sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(gc);
			Binding b;
			b.inputs[1] = &depth.srv;
			b.outputs[6] = mip > 0 ? &hzb_mip_uavs[mip - 1] : nullptr;
			b.outputs[7] = &hzb_mip_uavs[mip];
			command_list.set_pipeline(mip == 0 ? pipelines.hzb_first.get() : pipelines.hzb_down.get());
			bind(command_list, sg::PipelineType::Compute, cbv, b);
			const sg::u32 w = std::max(hzb_width >> mip, 1u), h = std::max(hzb_height >> mip, 1u);
			command_list.dispatch((w + 7) / 8, (h + 7) / 8, 1);
		}
	}

	void Renderer::render_geometry(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer)
	{
		sg::ShaderResourceView no_srv = {};
		command_list.clear_buffer_uint(cull_statistics.uav, no_srv, 0);
		command_list.clear_depth_stencil_view(depth.dsv, true, false, 0.0f); // Reverse Z: 0 is the far plane

		begin_timer(command_list, "Geometry (early)");
		render_geometry_phase(command_list, cbuffer, PBR_PHASE_EARLY);
		end_timer(command_list);
		if (cv_cull_occlusion)
		{
			begin_timer(command_list, "HZB");
			build_hzb(command_list, cbuffer);
			end_timer(command_list);
			begin_timer(command_list, "Geometry (late)");
			render_geometry_phase(command_list, cbuffer, PBR_PHASE_LATE);
			end_timer(command_list);
		}
		if (cv_debug_view == PBR_DEBUG_HZB && !cv_cull_occlusion)
		{
			build_hzb(command_list, cbuffer);
		}

		// Visibility buffer -> G-buffer
		begin_timer(command_list, "Resolve");
		gpu::PassConstants pc = {};
		sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(pc);
		Binding b;
		b.inputs[0] = &visibility.srv;
		b.inputs[1] = &depth.srv;
		b.outputs[0] = &gbuffer_albedo.uav;
		b.outputs[1] = &gbuffer_normal.uav;
		b.outputs[2] = &gbuffer_geometry.uav;
		b.outputs[3] = &gbuffer_emissive.uav;
		b.outputs[4] = &motion.uav;
		b.outputs[5] = &guide[current].uav;
		dispatch_screen(command_list, pipelines.resolve.get(), cbv, b);
		end_timer(command_list);
	}

	const Renderer::Target& Renderer::denoise(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer, DenoiseTargets& t, const Target& raw, sg::u32 signal, int iterations, bool enabled)
	{
		if (!enabled)
		{
			return raw;
		}
		const sg::u32 previous = current ^ 1;
		// Unfiltered history: the temporal pass accumulates raw samples (an unbiased running mean) and the spatial filter
		// only affects this frame's output
		const bool filtered_history = cv_denoise_filtered_history;
		gpu::PassConstants pc = {};
		pc.u0 = { signal, 1, 0, static_cast<sg::u32>(history_valid ? cv_denoise_history.get() : 1) };
		pc.f0 = { cv_denoise_alpha, cv_denoise_alpha_moments, cv_denoise_phi_colour, cv_denoise_phi_normal };
		pc.f1 = { cv_denoise_phi_depth, cv_denoise_spatial_variance, 0.0f, 0.0f };

		// Temporal accumulation
		{
			sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(pc);
			Binding b;
			b.inputs[0] = const_cast<sg::ShaderResourceView*>(&raw.srv);
			b.inputs[1] = &t.history[previous].srv;
			b.inputs[2] = &t.moments[previous].srv;
			b.inputs[3] = &motion.srv;
			b.inputs[4] = &guide[current].srv;
			b.inputs[5] = &guide[previous].srv;
			b.outputs[0] = filtered_history ? &t.accumulated.uav : &t.history[current].uav;
			b.outputs[1] = &t.moments[current].uav;
			dispatch_screen(command_list, pipelines.denoise_temporal.get(), cbv, b);
		}
		// Variance
		{
			sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(pc);
			Binding b;
			b.inputs[0] = filtered_history ? &t.accumulated.srv : &t.history[current].srv;
			b.inputs[2] = &t.moments[current].srv;
			b.inputs[4] = &guide[current].srv;
			b.outputs[0] = &t.ping.uav;
			dispatch_screen(command_list, pipelines.denoise_variance.get(), cbv, b);
		}
		// A-trous iterations (with filtered history, the first one's result is next frame's history)
		Target* source = &t.ping;
		Target* destination = &t.pong;
		for (int i = 0; i < iterations; i++)
		{
			pc.u0.y = 1u << i;
			pc.u0.z = i == 0 && filtered_history ? 1u : 0u;
			sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(pc);
			Binding b;
			b.inputs[0] = &source->srv;
			b.inputs[4] = &guide[current].srv;
			b.outputs[0] = &destination->uav;
			b.outputs[1] = i == 0 && filtered_history ? &t.history[current].uav : nullptr;
			dispatch_screen(command_list, pipelines.denoise_atrous.get(), cbv, b);
			std::swap(source, destination);
		}
		return *source;
	}

	void Renderer::render_lighting(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer)
	{
		// Pixel spread angle: the angle a pixel covers at the image centre
		const float ys = frame.projection._22;
		const float pixel_spread = 2.0f / (ys * height);

		begin_timer(command_list, "Ray Traced Lighting");
		{
			gpu::PassConstants pc = {};
			// Must match LIGHTING_* in pbrLighting.hlsl
			const sg::u32 options = (cv_emissive_at_hits ? 1u : 0u) | (cv_sun_shadows_at_hits ? 2u : 0u) | (cv_cache_propagation ? 4u : 0u);
			pc.u0 = { static_cast<sg::u32>(cv_emissive_candidates_secondary.get()), options, static_cast<sg::u32>(cv_cache_update_interval.get()), 0 };
			pc.f0 = { cv_diffuse_spread, cv_specular_spread, pixel_spread, cv_specular_max_roughness };
			sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(pc);
			Binding b;
			b.inputs[0] = &gbuffer_albedo.srv;
			b.inputs[1] = &gbuffer_normal.srv;
			b.inputs[2] = &gbuffer_geometry.srv;
			b.inputs[3] = &depth.srv;
			b.inputs[5] = &scene->get_views().emissive_triangles;
			b.outputs[0] = &shadow_raw.uav;
			b.outputs[1] = &diffuse_raw.uav;
			b.outputs[2] = &specular_raw.uav;
			b.outputs[3] = &cache_keys.uav;
			b.outputs[4] = &cache_accumulators.uav;
			b.outputs[5] = &cache_values.uav;
			dispatch_screen(command_list, pipelines.lighting.get(), cbv, b);

			if (cv_radiance_cache)
			{
				Binding cb;
				cb.outputs[3] = &cache_keys.uav;
				cb.outputs[4] = &cache_accumulators.uav;
				cb.outputs[5] = &cache_values.uav;
				command_list.set_pipeline(pipelines.cache_resolve.get());
				bind(command_list, sg::PipelineType::Compute, cbv, cb);
				command_list.dispatch((cache_capacity + 63) / 64, 1, 1);
			}
		}
		end_timer(command_list);

		begin_timer(command_list, "Denoise Shadow");
		const Target& shadow = denoise(command_list, cbuffer, shadow_denoise, shadow_raw, 2, cv_denoise_shadow_iterations, cv_denoise_shadow);
		end_timer(command_list);
		begin_timer(command_list, "Denoise Diffuse");
		const Target& diffuse = denoise(command_list, cbuffer, diffuse_denoise, diffuse_raw, 0, cv_denoise_diffuse_iterations, cv_denoise_diffuse);
		end_timer(command_list);
		begin_timer(command_list, "Denoise Specular");
		const Target& specular = denoise(command_list, cbuffer, specular_denoise, specular_raw, 1, cv_denoise_specular_iterations, cv_denoise_specular);
		end_timer(command_list);

		begin_timer(command_list, "Composite");
		gpu::PassConstants pc = {};
		pc.u0 = { gbuffer_albedo.bindless, gbuffer_normal.bindless, gbuffer_geometry.bindless, gbuffer_emissive.bindless };
		pc.u1 = { depth.bindless, shadow.bindless, diffuse.bindless, specular.bindless };
		pc.u2 = { shadow_raw.bindless, diffuse_raw.bindless, specular_raw.bindless, motion.bindless };
		pc.u3 = { diffuse_denoise.moments[current].bindless, hzb.bindless, visibility.bindless, 0 };
		sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(pc);
		Binding b;
		b.outputs[0] = &lit.uav;
		dispatch_screen(command_list, pipelines.composite.get(), cbv, b);
		end_timer(command_list);
	}

	void Renderer::render_path_tracer(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer)
	{
		begin_timer(command_list, "Path Tracer");
		const float ys = frame.projection._22;
		gpu::PassConstants pc = {};
		pc.u0 = { static_cast<sg::u32>(cv_pt_bounces.get()), static_cast<sg::u32>(cv_pt_rr.get()), accumulated_frames, static_cast<sg::u32>(cv_pt_max_frames.get()) };
		pc.f0 = { 2.0f / (ys * height), 0.0f, 0.0f, 0.0f };
		sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(pc);
		Binding b;
		b.inputs[5] = &scene->get_views().emissive_triangles;
		b.outputs[0] = &path_accumulation.uav;
		b.outputs[1] = &path_output.uav;
		dispatch_screen(command_list, pipelines.path_trace.get(), cbv, b);
		accumulated_frames = std::min<sg::u32>(accumulated_frames + 1, static_cast<sg::u32>(cv_pt_max_frames.get()));
		end_timer(command_list);
	}

	void Renderer::render(sg::CommandList& command_list, const Camera& camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, sg::UnorderedAccessView& output)
	{
		if (!supported)
		{
			return;
		}
		if (!resources_created)
		{
			create_resources();
			create_pipelines();
		}
		if (!pending_preset_load.empty())
		{
			load_scene(pending_preset_load, const_cast<Camera*>(&camera)); // The camera moves to the scene's view
			pending_preset_load.clear();
		}
		timers.clear();
		timestamp_pool->begin_frame();

		const bool was_ready = scene->is_ready();
		scene->update(command_list, upload_heap, cbuffer);
		if (!was_ready && scene->is_ready())
		{
			if (camera_on_ready)
			{
				apply_preset_camera(*camera_on_ready);
				camera_on_ready = nullptr;
			}
			// Per scene buffers: task regions (one per bucket, sized for every task an instance of the bucket can produce),
			// instance and meshlet visibility
			scene_version++;
			task_region_sizes = {};
			const sg::u32 max_tasks = scene->get_max_tasks();
			task_region_sizes[PBR_BUCKET_OPAQUE] = max_tasks;
			task_region_sizes[PBR_BUCKET_MASKED] = max_tasks;
			task_region_offsets = { 0, max_tasks, 0, 0 };
			tasks = create_uav_buffer(8, max_tasks * 2);
			instance_visibility = create_uav_buffer(4, scene->get_instance_count());
			meshlet_visibility = create_uav_buffer(4, (scene->get_meshlet_visibility_bits() + 31) / 32);
			sg::ShaderResourceView no_srv = {};
			command_list.clear_buffer_uint(instance_visibility.uav, no_srv, 0);
			command_list.clear_buffer_uint(meshlet_visibility.uav, no_srv, 0);
		}

		update_frame_constants(camera);
		if (seen_scene_version != scene_version || clear_radiance_cache)
		{
			seen_scene_version = scene_version;
			clear_radiance_cache = false;
			history_valid = false;
			sg::ShaderResourceView no_srv = {};
			command_list.clear_buffer_uint(cache_keys.uav, no_srv, 0);
			command_list.clear_buffer_uint(cache_accumulators.uav, no_srv, 0);
			command_list.clear_buffer_float(cache_values.uav, no_srv, 1000.0f);
		}
		frame_cbv = cbuffer.AllocateAndWrite(frame);

		begin_timer(command_list, "Sky");
		render_sky(command_list, cbuffer);
		end_timer(command_list);

		const Target* hdr = nullptr;
		if (scene->is_ready())
		{
			render_geometry(command_list, cbuffer);
			if (cv_mode == 1)
			{
				render_path_tracer(command_list, cbuffer);
				hdr = &path_output;
			}
			else
			{
				render_lighting(command_list, cbuffer);
				// Temporal anti-aliasing
				begin_timer(command_list, "TAA");
				gpu::PassConstants pc = {};
				pc.u0 = { lit.bindless, taa_history[current ^ 1].bindless, motion.bindless, depth.bindless };
				pc.u1 = { history_valid && cv_taa ? 1u : 0u, 0, 0, 0 };
				pc.f0 = { cv_taa_blend, cv_taa_gamma, 0.0f, 0.0f };
				sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(pc);
				Binding b;
				b.outputs[0] = &taa_history[current].uav;
				dispatch_screen(command_list, pipelines.taa.get(), cbv, b);
				end_timer(command_list);
				hdr = &taa_history[current];
			}
		}
		else
		{
			// Loading: show the sky
			hdr = nullptr;
		}

		begin_timer(command_list, "Exposure + Tone Map");
		{
			sg::ShaderResourceView no_srv = {};
			gpu::PassConstants pc = {};
			pc.u0 = { hdr ? hdr->bindless : dummy_texture.bindless, 0, 0, 0 };
			pc.u1 = { static_cast<sg::u32>(cv_tonemap.get()), 0, 0, 0 };
			pc.f0 = { kHistogramMinLog2, kHistogramRangeLog2, 0.0f, 0.0f };
			if (hdr)
			{
				command_list.clear_buffer_uint(histogram.uav, no_srv, 0);
				sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(pc);
				Binding b;
				b.outputs[1] = &histogram.uav;
				command_list.set_pipeline(pipelines.histogram.get());
				bind(command_list, sg::PipelineType::Compute, cbv, b);
				command_list.dispatch((width + 15) / 16, (height + 15) / 16, 1);
			}
			pc.f0 = { cv_dither, 0.0f, 0.0f, 0.0f };
			sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(pc);
			Binding b;
			b.outputs[2] = &output;
			dispatch_screen(command_list, pipelines.tonemap.get(), cbv, b);
			// Release the output UAV (it is the application's render target)
			Binding none;
			bind(command_list, sg::PipelineType::Compute, cbv, none);
			if (hdr)
			{
				command_list.copy_buffer_to_buffer(histogram_readback.get(), histogram.buffer.get());
				command_list.copy_buffer_to_buffer(statistics_readback.get(), cull_statistics.buffer.get());
			}
			readback_valid = hdr != nullptr;
		}
		end_timer(command_list);
		timestamp_pool->end_frame(&command_list);
		frame_recorded = true;

		history_valid = scene->is_ready();
		current ^= 1;
		frame_index++;
	}
}
