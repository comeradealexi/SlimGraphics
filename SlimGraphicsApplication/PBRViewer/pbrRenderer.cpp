#include "pbrRenderer.h"
#include "CVars.h"
#include "ImGuiLayout.h"
#include <sgUtils.h>
#include <Utils.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iterator>

using namespace DirectX;

namespace pbr
{
	namespace
	{
		// --- Settings (every one is a cvar: the "PBR Settings" window, -cvar=name=value, test plans)
		CVarInt cv_mode("pbr.mode", "General", "Real time (rasterised visibility buffer + ray traced lighting) or the progressive reference path tracer",
			0, { "Real Time", "Path Traced" }, CVarResetHistory);
		// In PBR_DEBUG_* order
		CVarInt cv_debug_view("pbr.debug_view", "General", "What the viewer shows",
			0, { "Final", "White Furnace", "Direct Lighting", "Indirect Lighting", "Diffuse Indirect (Raw)", "Diffuse Indirect (Denoised)", "Specular (Raw)",
			"Specular (Denoised)", "Emissive", "Radiance Cache", "Albedo", "Normals", "Geometric Normals", "Roughness", "Metallic", "Ambient Occlusion",
			"Motion Vectors", "Depth", "Instances", "Meshlets", "Triangles", "Shadow (Raw)", "Shadow (Denoised)", "History Length", "Diffuse Variance", "HZB" },
			CVarResetHistory);

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

		constexpr sg::u32 kMaxTimers = 64;

		// What each debug view shows (PBR_DEBUG_* order), for the PBR Viewer window
		const char* const kDebugViewDescriptions[] =
		{
			"The finished image: emitted light, direct sunlight and denoised indirect light, anti-aliased, exposed and tone mapped.",
			"Every surface white, lit only by a uniform white sky. An energy conserving renderer shows an even image: darker or brighter areas reveal lost or invented energy.",
			"Sunlight only: the BRDF times the sun's illuminance, times the ray traced shadow.",
			"Light that bounced off other surfaces: the denoised diffuse and specular signals weighted by the material.",
			"One cosine distributed ray per pixel before denoising: the noise the denoiser has to remove.",
			"Incoming diffuse light after temporal accumulation and the edge aware filter. It is divided by the albedo, so the filter blurs light, not texture detail.",
			"One reflection ray per pixel, sampled from the GGX lobe, before denoising.",
			"Reflected light after denoising. Smooth surfaces keep sharp reflections; rough ones are filtered wider.",
			"Light emitted by surfaces such as lamps and windows.",
			"The world space radiance cache read at each surface: a hash grid that stores light for further bounces. Cells grow with distance from the camera.",
			"Base colour: what the surface reflects, without any lighting.",
			"Shading normals including normal maps (x, y, z shown as red, green, blue).",
			"Triangle face normals. Compare with Normals to see the detail normal maps add.",
			"Perceptual roughness: black is mirror like, white is matte.",
			"Metalness: white surfaces are metals (coloured reflections, no diffuse light).",
			"Ambient occlusion baked into the material textures.",
			"Screen space motion of each pixel since the last frame (move the camera). TAA and the denoiser use it to reproject their history.",
			"Distance as repeating bands (log2 of the linear depth). The depth buffer is reverse Z with an infinite far plane.",
			"Each colour is one instance: one mesh placed in the world, with its own bottom level acceleration structure.",
			"Each colour is one meshlet (up to 64 vertices and 124 triangles): the unit the amplification shader culls and one mesh shader group draws.",
			"Each colour is one triangle, read back from the visibility buffer's ids.",
			"One shadow ray per pixel towards a random point on the sun's disk: soft shadow noise before denoising.",
			"Sun visibility after denoising.",
			"Frames of history the denoiser has accumulated (blue: new, red: 32 frames). Newly revealed areas start over.",
			"The diffuse signal's estimated noise: where the denoiser filters hardest.",
			"The hierarchical depth buffer occlusion culling tests against, cycling through its levels. Each texel holds the farthest depth of the area it covers.",
		};
		static_assert(sizeof(kDebugViewDescriptions) / sizeof(kDebugViewDescriptions[0]) == PBR_DEBUG_HZB + 1, "One description per debug view");

		// What each timed pass does on the GPU
		const char* pass_description(const char* pass)
		{
			static const std::pair<const char*, const char*> descriptions[] =
			{
				{ "Sky", "Sky radiance: single scattering through the atmosphere plus a multiple scattering lookup table, into an equirectangular texture." },
				{ "Geometry (early)", "Draws what was visible last frame: instance culling in a compute shader, meshlet culling in the amplification shader, then mesh shaders write instance and triangle ids into the visibility buffer." },
				{ "HZB", "Builds the depth pyramid of the early pass that occlusion culling tests against." },
				{ "Geometry (late)", "Tests everything not drawn yet against the depth pyramid and draws what became visible." },
				{ "Resolve", "Turns the visibility buffer's ids into a G-buffer: re-intersects each pixel's triangle for exact barycentrics and texture gradients, and evaluates the material once per pixel." },
				{ "Ray Traced Lighting", "Inline ray queries per pixel: a sun shadow ray, a diffuse ray, a reflection ray and an emissive light sample, plus radiance cache updates." },
				{ "Denoise Shadow", "Temporal accumulation, then an edge aware a-trous wavelet filter (SVGF), for the sun shadow." },
				{ "Denoise Diffuse", "Temporal accumulation, then an edge aware a-trous wavelet filter (SVGF), for diffuse light." },
				{ "Denoise Specular", "Temporal accumulation (reprojecting reflections by their hit distance), then an edge aware a-trous filter that narrows on smooth surfaces." },
				{ "Composite", "Combines the materials with direct sunlight and the denoised indirect light." },
				{ "TAA", "Temporal anti-aliasing: blends with the reprojected previous frame, clipped to the colours around each pixel." },
				{ "Exposure + Tone Map", "A luminance histogram for automatic exposure, then the tone mapping curve and dithering." },
				{ "Path Tracer", "One complete light path per pixel per frame, averaged over frames: the ground truth the real time image is compared with." },
			};
			for (const auto& [name, description] : descriptions)
			{
				if (strcmp(name, pass) == 0)
				{
					return description;
				}
			}
			return "";
		}

		void disabled_text(const char* text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
			ImGui::TextWrapped("%s", text);
			ImGui::PopStyleColor();
		}

		void tooltip(const char* text)
		{
			if (ImGui::IsItemHovered() && text[0] != '\0')
			{
				ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f));
				ImGui::BeginTooltip();
				ImGui::TextWrapped("%s", text);
				ImGui::EndTooltip();
			}
		}

		sg::u32 groups(sg::u32 threads, sg::u32 group_size)
		{
			return (threads + group_size - 1) / group_size;
		}

		float halton(sg::u32 index, sg::u32 base)
		{
			float f = 1.0f, r = 0.0f;
			for (; index > 0; index /= base)
			{
				f /= static_cast<float>(base);
				r += f * static_cast<float>(index % base);
			}
			return r;
		}

		// Sun transmittance through the atmosphere from the viewer (the model of pbrSky.hlsl)
		XMFLOAT3 atmosphere_transmittance(XMVECTOR sun, float rayleigh_scale, float mie_scale, float ozone_scale)
		{
			const XMVECTOR origin = XMVectorSet(0.0f, PBR_EARTH_RADIUS + PBR_VIEWER_ALTITUDE, 0.0f, 0.0f);
			const float b = XMVectorGetX(XMVector3Dot(origin, sun));
			const float c = XMVectorGetX(XMVector3Dot(origin, origin));
			if (b < 0.0f && b * b - (c - PBR_EARTH_RADIUS * PBR_EARTH_RADIUS) > 0.0f)
			{
				return { 0.0f, 0.0f, 0.0f }; // Below the horizon: the Earth blocks the sun
			}
			const int steps = 64;
			const float dt = (-b + std::sqrt(b * b - (c - PBR_ATMOSPHERE_RADIUS * PBR_ATMOSPHERE_RADIUS))) / steps;
			XMVECTOR optical_depth = XMVectorZero();
			for (int i = 0; i < steps; i++)
			{
				const float altitude = XMVectorGetX(XMVector3Length(XMVectorAdd(origin, XMVectorScale(sun, (i + 0.5f) * dt)))) - PBR_EARTH_RADIUS;
				const float rayleigh = std::exp(-altitude / PBR_RAYLEIGH_SCALE_HEIGHT) * rayleigh_scale;
				const float mie = std::exp(-altitude / PBR_MIE_SCALE_HEIGHT) * mie_scale;
				const float ozone = std::max(0.0f, 1.0f - std::abs(altitude - PBR_OZONE_CENTRE) / PBR_OZONE_WIDTH) * ozone_scale;
				const XMVECTOR extinction = XMVectorAdd(XMVectorScale(XMVectorSet(PBR_RAYLEIGH_SCATTERING_R, PBR_RAYLEIGH_SCATTERING_G, PBR_RAYLEIGH_SCATTERING_B, 0.0f), rayleigh),
					XMVectorAdd(XMVectorReplicate(PBR_MIE_EXTINCTION * mie), XMVectorScale(XMVectorSet(PBR_OZONE_ABSORPTION_R, PBR_OZONE_ABSORPTION_G, PBR_OZONE_ABSORPTION_B, 0.0f), ozone)));
				optical_depth = XMVectorAdd(optical_depth, XMVectorScale(extinction, dt));
			}
			XMFLOAT3 depth;
			XMStoreFloat3(&depth, optical_depth);
			return { std::exp(-depth.x), std::exp(-depth.y), std::exp(-depth.z) };
		}

		// Frustum planes (normals inwards) of a row vector view projection: clip = p * M, so they come from M's columns
		void frustum_planes(FXMMATRIX view_projection, std::array<XMFLOAT4, 6>& planes)
		{
			const XMMATRIX t = XMMatrixTranspose(view_projection);
			const XMVECTOR p[6] =
			{
				XMVectorAdd(t.r[3], t.r[0]), XMVectorSubtract(t.r[3], t.r[0]),	// Left, right
				XMVectorAdd(t.r[3], t.r[1]), XMVectorSubtract(t.r[3], t.r[1]),	// Bottom, top
				XMVectorSubtract(t.r[3], t.r[2]),								// Near (reverse Z: depth <= 1)
				t.r[2],															// Far (reverse Z: depth >= 0; at infinity, always passes)
			};
			for (int i = 0; i < 6; i++)
			{
				const float length = XMVectorGetX(XMVector3Length(p[i]));
				XMStoreFloat4(&planes[i], length > 1e-6f ? XMVectorScale(p[i], 1.0f / length) : XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f));
			}
		}

		// Left handed, reverse Z, infinite far plane (depth = near / z), jittered by a sub pixel offset
		XMMATRIX projection(float x_scale, float y_scale, float near_plane, XMFLOAT2 jitter, sg::u32 width, sg::u32 height)
		{
			return XMMATRIX(
				x_scale, 0.0f, 0.0f, 0.0f,
				0.0f, y_scale, 0.0f, 0.0f,
				2.0f * jitter.x / width, -2.0f * jitter.y / height, 0.0f, 1.0f,
				0.0f, 0.0f, near_plane, 0.0f);
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

	// ---------------------------------------------------------------------------------------------------------------------
	// Resources

	Renderer::Target Renderer::create_target(sg::u32 w, sg::u32 h, DXGI_FORMAT format, sg::ResourceUsageFlags usage, sg::u32 mip_count)
	{
		auto has = [&](sg::ResourceUsageFlags flag) { return (usage & flag) == flag; };
		sg::ResourceCreateDesc desc(w, h, format, usage);
		desc.mip_count = mip_count;
		desc.optimised_clear_depth = 0.0f; // Reverse Z clears depth to 0 (the far plane)
		Target t;
		t.texture = sg::create_texture(*device, desc);
		t.srv = device->create_shader_resource_view(t.texture);
		t.uav = has(sg::ResourceUsageFlags::UnorderedAccess) ? device->create_unordered_access_view(t.texture) : sg::UnorderedAccessView();
		t.rtv = has(sg::ResourceUsageFlags::RenderTarget) ? device->create_render_target_view(t.texture) : nullptr;
		t.dsv = has(sg::ResourceUsageFlags::DepthStencil) ? device->create_depth_stencil_view(t.texture) : nullptr;
		t.bindless = device->allocate_bindless_descriptor(t.srv);
		return t;
	}

	Renderer::BufferResource Renderer::create_buffer(sg::u32 element_size, sg::u32 element_count)
	{
		element_count = std::max(element_count, 1u);
		BufferResource b;
		b.buffer = create_padded_buffer(*device, static_cast<size_t>(element_size) * element_count, sg::BufferType::GeneralDataBuffer, true);
		b.uav = device->create_unordered_access_view(b.buffer, element_size, element_count);
		b.srv = device->create_shader_resource_view(b.buffer, element_size, element_count);
		return b;
	}

	void Renderer::create_resources()
	{
		using F = sg::ResourceUsageFlags;
		const F uav = F::UnorderedAccess;
		auto screen_target = [&](DXGI_FORMAT format, F usage = F::UnorderedAccess) { return create_target(width, height, format, usage); };

		depth = screen_target(DXGI_FORMAT_D32_FLOAT, F::DepthStencil);
		visibility = screen_target(DXGI_FORMAT_R32G32_UINT, F::RenderTarget);
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

		const DXGI_FORMAT half4 = DXGI_FORMAT_R16G16B16A16_FLOAT;
		for (Target* t : { &gbuffer_albedo, &gbuffer_normal, &gbuffer_geometry, &gbuffer_emissive, &diffuse_raw, &specular_raw, &lit, &taa_history[0], &taa_history[1], &path_output })
		{
			*t = screen_target(half4);
		}
		motion = screen_target(DXGI_FORMAT_R16G16_FLOAT);
		guide[0] = screen_target(DXGI_FORMAT_R32G32_UINT);
		guide[1] = screen_target(DXGI_FORMAT_R32G32_UINT);
		shadow_raw = screen_target(DXGI_FORMAT_R16_FLOAT);
		path_accumulation = screen_target(DXGI_FORMAT_R32G32B32A32_FLOAT);
		for (DenoiseTargets* d : { &shadow_denoise, &diffuse_denoise, &specular_denoise })
		{
			for (Target* t : { &d->accumulated, &d->history[0], &d->history[1], &d->ping, &d->pong })
			{
				*t = screen_target(half4);
			}
			d->moments[0] = screen_target(DXGI_FORMAT_R32G32B32A32_FLOAT);
			d->moments[1] = screen_target(DXGI_FORMAT_R32G32B32A32_FLOAT);
		}
		sky = create_target(sky_width, sky_height, half4, uav);
		sky_multiple_scattering = create_target(32, 32, DXGI_FORMAT_R32G32B32A32_FLOAT, uav);
		dummy_texture = create_target(1, 1, half4, F::None);

		task_counts = create_buffer(4, 4);
		draw_arguments = create_buffer(16, PBR_BUCKET_COUNT);
		cull_statistics = create_buffer(4, PBR_STAT_COUNT);
		histogram = create_buffer(4, 256);
		cache_keys = create_buffer(4, cache_capacity);
		cache_accumulators = create_buffer(16, cache_capacity);
		cache_values = create_buffer(16, cache_capacity);
		dummy_srv_buffer = create_buffer(16, 1);
		dummy_uav_buffer = create_buffer(16, 1);
		statistics_readback = sg::create_readback_buffer(*device, 256);
		histogram_readback = sg::create_readback_buffer(*device, 1024);
		resources_created = true;
	}

	void Renderer::create_pipelines()
	{
		auto compute = [&](const char* shader)
			{
				sg::PipelineDesc::Compute desc;
				desc.compute_shader = sg::create_compute_shader(*device, get_shader_path(shader));
				sg::SharedPtr<sg::Pipeline> pipeline = device->create_pipeline(desc, pass_binding_desc);
				seAssert(pipeline != nullptr, "Failed to create %s", shader);
				return pipeline;
			};
		pipelines.cull_instances = compute("PBR_CullInstances");
		pipelines.build_draw_arguments = compute("PBR_BuildDrawArguments");
		pipelines.hzb_first = compute("PBR_BuildHZBFirst");
		pipelines.hzb_down = compute("PBR_BuildHZBDown");
		pipelines.resolve = compute("PBR_Resolve");
		pipelines.sky = compute("PBR_Sky");
		pipelines.sky_multiple_scattering = compute("PBR_SkyMultipleScattering");
		pipelines.lighting = compute("PBR_Lighting");
		pipelines.cache_resolve = compute("PBR_RadianceCacheResolve");
		pipelines.denoise_temporal = compute("PBR_DenoiseTemporal");
		pipelines.denoise_variance = compute("PBR_DenoiseVariance");
		pipelines.denoise_atrous = compute("PBR_DenoiseAtrous");
		pipelines.composite = compute("PBR_Composite");
		pipelines.taa = compute("PBR_TAA");
		pipelines.histogram = compute("PBR_Histogram");
		pipelines.tonemap = compute("PBR_Tonemap");
		pipelines.path_trace = compute("PBR_PathTrace");

		// One visibility pipeline per bucket: the masked bucket draws both sides and alpha tests in its pixel shader
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

	// Per scene buffers: task regions (one per bucket, sized for every task an instance of the bucket can produce), instance
	// and meshlet visibility
	void Renderer::create_scene_resources()
	{
		const sg::u32 max_tasks = scene->get_max_tasks();
		task_region_sizes = { max_tasks, max_tasks, 0, 0 };
		task_region_offsets = { 0, max_tasks, 0, 0 };
		tasks = create_buffer(8, max_tasks * PBR_BUCKET_COUNT);
		instance_visibility = create_buffer(4, scene->get_instance_count());
		meshlet_visibility = create_buffer(4, (scene->get_meshlet_visibility_bits() + 31) / 32);
		clear(instance_visibility);
		clear(meshlet_visibility);
		reset_history();
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// Scenes and tests

	bool Renderer::load_scene(const std::string& name_or_path, Camera* camera)
	{
		if (!supported)
		{
			return false;
		}
		const ScenePreset* preset = find_scene_preset(name_or_path);
		const std::string path = preset ? preset->path : name_or_path;
		// The camera moves to the scene's view once it is ready (file cameras are known after the import)
		camera_on_ready = camera;
		if (scene->is_loaded(path))
		{
			// Another preset of the same file: keep the scene
			scene->set_preset(preset);
			reset_history();
			if (camera && scene->is_ready())
			{
				apply_preset_camera(*camera);
				camera_on_ready = nullptr;
			}
		}
		else
		{
			scene->load(path, preset);
		}
		if (preset)
		{
			cv_sun_elevation.set(preset->sun_elevation);
			cv_sun_azimuth.set(preset->sun_azimuth);
		}
		camera_track.update(*scene);
		return true;
	}

	void Renderer::apply_preset_camera(Camera& camera) const
	{
		XMFLOAT3 position, target;
		if (scene && scene->get_start_camera(position, target))
		{
			camera.SetLookAt(position, target);
		}
		if (scene && scene->is_ready())
		{
			// Crossing the scene takes about 20 seconds
			camera.MovementSpeed() = std::clamp(scene->get_bounds().radius() * 0.1f, 0.5f, 50.0f);
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
		reset_history();
		exposure_initialised = false;
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// Per frame state

	void Renderer::update(float delta_time, Camera& camera)
	{
		if (!supported)
		{
			return;
		}
		read_back_results(delta_time);
		camera_track.update(*scene);
		imgui(camera);
	}

	// Results of the previous frame (the application waits for each frame to finish)
	void Renderer::read_back_results(float delta_time)
	{
		if (!frame_recorded)
		{
			return;
		}
		timestamp_pool->resolve();
		// Averaged over recent frames (single frame GPU timings are noisy)
		std::vector<std::pair<const char*, double>> previous = std::move(timings);
		timings.clear();
		for (const Timer& t : timers)
		{
			const double us = timestamp_pool->collect_timestamp_us(t.index);
			auto average = std::find_if(previous.begin(), previous.end(), [&](const auto& p) { return strcmp(p.first, t.name) == 0; });
			timings.push_back({ t.name, average != previous.end() ? average->second + (us - average->second) * 0.1 : us });
		}
		if (readback_valid)
		{
			statistics_readback->read_memory(0, cull_stats.data(), sizeof(sg::u32) * PBR_STAT_COUNT);
		}
		update_exposure(delta_time);
		frame_recorded = false;
	}

	void Renderer::update_exposure(float delta_time)
	{
		float target_ev100 = cv_ev100;
		if (cv_auto_exposure && readback_valid)
		{
			// Average log2 luminance of the pixels between the 40th and 95th percentiles (ignoring black pixels)
			std::array<sg::u32, 256> bins = {};
			histogram_readback->read_memory(0, bins.data(), sizeof(bins));
			double total = 0.0;
			for (sg::u32 i = 1; i < 256; i++) total += bins[i];
			const double low = total * 0.4, high = total * 0.95;
			double count = 0.0, sum = 0.0, weight = 0.0;
			for (sg::u32 i = 1; i < 256; i++)
			{
				const double inside = std::max(0.0, std::min(count + bins[i], high) - std::max(count, low));
				sum += (PBR_HISTOGRAM_MIN_LOG2 + (i - 0.5) / 254.0 * PBR_HISTOGRAM_RANGE_LOG2) * inside;
				weight += inside;
				count += bins[i];
			}
			if (weight > 0.0)
			{
				// EV100 = log2(L * S / K), S = 100, K = 12.5. The histogram holds pre-exposure luminance.
				target_ev100 = std::clamp(static_cast<float>(sum / weight + std::log2(100.0 / 12.5)), cv_exposure_min.get(), cv_exposure_max.get());
			}
		}
		if (!exposure_initialised || !cv_auto_exposure || cv_exposure_speed <= 0.0f)
		{
			exposure_ev100 = target_ev100;
			exposure_initialised = cv_auto_exposure ? readback_valid : true;
		}
		else
		{
			exposure_ev100 += (target_ev100 - exposure_ev100) * (1.0f - std::exp(-delta_time * cv_exposure_speed));
		}
		// Photometric exposure of a camera with this EV100 (saturation based: 1.2 * 2^EV100 maps to white)
		exposure = std::exp2(cv_exposure_compensation.get()) / (1.2f * std::exp2(exposure_ev100));
	}

	void Renderer::update_sun()
	{
		const float elevation = XMConvertToRadians(cv_sun_elevation);
		const float azimuth = XMConvertToRadians(cv_sun_azimuth);
		const float radius = XMConvertToRadians(cv_sun_radius);
		const XMVECTOR sun = XMVectorSet(std::cos(elevation) * std::cos(azimuth), std::sin(elevation), std::cos(elevation) * std::sin(azimuth), 0.0f);
		// The white furnace view is lit by the uniform sky alone
		const XMFLOAT3 transmittance = cv_debug_view == PBR_DEBUG_WHITE_FURNACE ? XMFLOAT3(0.0f, 0.0f, 0.0f) : atmosphere_transmittance(sun, cv_sky_rayleigh, cv_sky_mie, cv_sky_ozone);
		const XMVECTOR illuminance = XMVectorScale(XMLoadFloat3(&transmittance), cv_sun_illuminance);
		XMStoreFloat4(&frame.sun_direction, XMVectorSetW(sun, std::cos(radius)));
		XMStoreFloat4(&frame.sun_illuminance, illuminance);
		// Radiance of a uniform disk with that illuminance: E / solid angle
		XMStoreFloat4(&frame.sun_disk_radiance, XMVectorScale(illuminance, 1.0f / (XM_2PI * (1.0f - std::cos(radius)))));
	}

	void Renderer::update_frame_constants(const Camera& camera)
	{
		// Sub pixel jitter (Halton 2, 3) for temporal anti-aliasing; none for the path tracer (it jitters per sample) or views
		// of data
		const bool lighting_view = static_cast<sg::u32>(cv_debug_view.get()) < PBR_DEBUG_FIRST_DATA_VIEW;
		const sg::u32 jitter_index = (frame_index % 16) + 1;
		const XMFLOAT2 jitter = cv_taa && cv_mode == 0 && lighting_view ? XMFLOAT2(halton(jitter_index, 2) - 0.5f, halton(jitter_index, 3) - 0.5f) : XMFLOAT2(0.0f, 0.0f);
		const ShaderStructs::CameraData& camera_data = camera.GetCameraShaderData();
		const float y_scale = XMVectorGetY(camera_data.projection_matrix.r[1]);	// 1 / tan(fov / 2)
		const float x_scale = y_scale * static_cast<float>(height) / static_cast<float>(width);
		const float near_plane = camera.GetNearPlane();
		const XMMATRIX view = camera_data.view_matrix;
		const XMMATRIX jittered_projection = projection(x_scale, y_scale, near_plane, jitter, width, height);
		const XMMATRIX view_projection = XMMatrixMultiply(view, jittered_projection);
		const XMMATRIX view_projection_no_jitter = XMMatrixMultiply(view, projection(x_scale, y_scale, near_plane, { 0.0f, 0.0f }, width, height));
		// The eye of the view matrix: Camera::Update moves its position after building the matrix, so GetPosition() is a
		// step ahead while the camera moves, and camera rays from it would not match the rasterised frame (textures slid)
		XMFLOAT4 camera_position;
		XMStoreFloat4(&camera_position, XMVectorSetW(XMMatrixInverse(nullptr, view).r[3], near_plane));

		// The path tracer starts over when the view changes; everything temporal when a setting that changes the image does
		XMFLOAT4X4 view_projection_stored;
		XMStoreFloat4x4(&view_projection_stored, view_projection_no_jitter);
		if (memcmp(&view_projection_stored, &previous_view_projection, sizeof(view_projection_stored)) != 0)
		{
			accumulated_frames = 0;
		}
		if (CVars::get_change_count(CVarResetHistory) != seen_reset_count)
		{
			seen_reset_count = CVars::get_change_count(CVarResetHistory);
			reset_history();
		}

		XMStoreFloat4x4(&frame.view, view);
		XMStoreFloat4x4(&frame.projection, jittered_projection);
		XMStoreFloat4x4(&frame.view_projection, view_projection);
		frame.view_projection_no_jitter = view_projection_stored;
		frame.previous_view_projection_no_jitter = history_valid ? previous_view_projection : view_projection_stored;
		previous_view_projection = view_projection_stored;
		frame.camera_position = camera_position;
		if (!cv_cull_freeze || frame_index == 0)
		{
			culling_camera_position = camera_position;
			frustum_planes(view_projection_no_jitter, culling_planes);
		}
		frame.culling_camera_position = culling_camera_position;
		std::copy(culling_planes.begin(), culling_planes.end(), frame.frustum_planes);
		frame.resolution = { static_cast<float>(width), static_cast<float>(height) };
		frame.inverse_resolution = { 1.0f / width, 1.0f / height };
		update_sun();

		const std::pair<bool, sg::u32> flags[] =
		{
			{ cv_cull_frustum, PBR_FLAG_FRUSTUM_CULLING }, { cv_cull_cone, PBR_FLAG_CONE_CULLING }, { cv_cull_occlusion, PBR_FLAG_OCCLUSION_CULLING },
			{ cv_shadows, PBR_FLAG_SHADOWS }, { cv_diffuse_gi, PBR_FLAG_DIFFUSE_GI }, { cv_specular, PBR_FLAG_SPECULAR },
			{ cv_radiance_cache, PBR_FLAG_RADIANCE_CACHE }, { cv_sky_lighting, PBR_FLAG_SKY_LIGHTING }, { cv_emissive, PBR_FLAG_EMISSIVE },
			{ cv_textures, PBR_FLAG_TEXTURES }, { cv_normal_maps, PBR_FLAG_NORMAL_MAPS }, { cv_alpha_test, PBR_FLAG_ALPHA_TEST }, { cv_sun_disk, PBR_FLAG_SUN_DISK },
		};
		frame.flags = 0;
		for (const auto& [enabled, flag] : flags)
		{
			frame.flags |= enabled ? flag : 0u;
		}
		frame.frame_index = frame_index;
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
		frame.alpha_test_lod_bias = cv_alpha_lod_bias;
		frame.alpha_tests_shadow = static_cast<sg::u32>(cv_alpha_tests_primary.get());
		frame.alpha_tests_indirect = static_cast<sg::u32>(cv_alpha_tests_secondary.get());
		frame.emissive_triangle_count = cv_emissive_sampling && scene->is_ready() ? scene->get_emissive_triangle_count() : 0;
		frame.emissive_candidates = static_cast<sg::u32>(cv_emissive_candidates.get());
		frame.emissive_weight = scene->get_emissive_weight();
		frame.cache_cell_size = cv_cache_cell;
		frame.cache_min_cell_size = cv_cache_min_cell;
		frame.cache_blend = cv_cache_blend;
		frame.cache_capacity = cache_capacity;
	}

	void Renderer::imgui(Camera& camera)
	{
		place_window_first_use(10.0f, 540.0f, 470.0f);
		CVars::imgui_window("PBR Settings", "pbr.");
		place_window_first_use(10.0f, 10.0f, 470.0f, 520.0f);
		if (ImGui::Begin("PBR Viewer"))
		{
			static int selected = 0;
			const std::vector<ScenePreset>& presets = scene_presets();
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::BeginCombo("##scene", presets[selected].name))
			{
				for (int i = 0; i < static_cast<int>(presets.size()); i++)
				{
					selected = ImGui::Selectable(presets[i].name, i == selected) ? i : selected;
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
			// What the viewer shows, explained
			ImGui::TextUnformatted("View");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(260.0f);
			cv_debug_view.imgui();
			disabled_text(kDebugViewDescriptions[std::clamp(cv_debug_view.get(), 0, static_cast<int>(PBR_DEBUG_HZB))]);
			if (ImGui::CollapsingHeader("GPU Timings", ImGuiTreeNodeFlags_DefaultOpen))
			{
				// Each pass and its share of the frame (hover a pass for what it does)
				const double total = gpu_milliseconds();
				if (ImGui::BeginTable("Timings", 3, ImGuiTableFlags_SizingFixedFit))
				{
					ImGui::TableSetupColumn("Pass");
					ImGui::TableSetupColumn("ms");
					ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthStretch);
					for (const auto& [name, us] : timings)
					{
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(name);
						tooltip(pass_description(name));
						ImGui::TableNextColumn();
						ImGui::Text("%6.2f", us / 1000.0);
						ImGui::TableNextColumn();
						ImGui::ProgressBar(total > 0.0 ? static_cast<float>(us / 1000.0 / total) : 0.0f, ImVec2(-1.0f, ImGui::GetTextLineHeight()), "");
					}
					ImGui::EndTable();
				}
				ImGui::Text("Total %.2f ms on the GPU", total);
#ifdef _DEBUG
				disabled_text("Debug build: the GPU runs at low, stable clocks with validation on, so timings are higher than in Release.");
#endif
			}
			if (ImGui::CollapsingHeader("Culling", ImGuiTreeNodeFlags_DefaultOpen))
			{
				// The funnel from every instance and meshlet to what was drawn
				const Scene::Statistics& s = scene->get_statistics();
				const sg::u32 drawn = cull_stats[PBR_STAT_MESHLETS_DRAWN_EARLY] + cull_stats[PBR_STAT_MESHLETS_DRAWN_LATE];
				auto percent = [](sg::u32 part, sg::u32 whole) { return whole > 0 ? 100.0f * part / whole : 0.0f; };
				ImGui::Text("Instances: %u, outside the view %u, hidden behind others %u", s.instances, cull_stats[PBR_STAT_INSTANCES_FRUSTUM_CULLED], cull_stats[PBR_STAT_INSTANCES_OCCLUSION_CULLED]);
				tooltip("Whole objects tested in a compute shader against the view frustum, then against the depth pyramid of what was drawn first.");
				ImGui::Text("Meshlets culled: frustum %u, facing away %u, occluded %u", cull_stats[PBR_STAT_MESHLETS_FRUSTUM_CULLED], cull_stats[PBR_STAT_MESHLETS_CONE_CULLED], cull_stats[PBR_STAT_MESHLETS_OCCLUSION_CULLED]);
				tooltip("Meshlets of the surviving instances tested in the amplification shader: frustum, normal cone (all triangles face away) and depth pyramid.");
				ImGui::Text("Drawn: %u of %u meshlets (%.1f%%), %u triangles (%.1f%%)", drawn, s.meshlets, percent(drawn, s.meshlets), cull_stats[PBR_STAT_TRIANGLES_DRAWN], percent(cull_stats[PBR_STAT_TRIANGLES_DRAWN], s.triangles));
				tooltip("Early: drawn because they were visible last frame. Late: became visible this frame. Only these reach the mesh shaders and rasteriser.");
				ImGui::Text("Early %u, late %u", cull_stats[PBR_STAT_MESHLETS_DRAWN_EARLY], cull_stats[PBR_STAT_MESHLETS_DRAWN_LATE]);
			}
			camera_track.imgui(camera);
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
			std::transform(key.begin(), key.end(), key.begin(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_'; });
			char value[64];
			sprintf_s(value, " pbr_ms_%s=%.2f", key.c_str(), us / 1000.0);
			line += value;
		}
		return line;
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// Recording

	void Renderer::bind(sg::PipelineType type, sg::ConstantBufferView pass_constants, const Binding& binding)
	{
		sg::Binding b;
		b.cbv_binding_count = pass_binding_desc.cbv_binding_count;
		b.srv_binding_count = pass_binding_desc.srv_binding_count;
		b.uav_binding_count = pass_binding_desc.uav_binding_count;
		b.sampler_binding_count = pass_binding_desc.sampler_binding_count;
		b.set_cbv(frame_cbv, 0);
		b.set_cbv(pass_constants, 1);
		Scene::Views* views = scene->is_ready() ? &scene->get_views() : nullptr;
		sg::ShaderResourceView* scene_views[9] = {};
		if (views)
		{
			sg::ShaderResourceView* v[9] = { &views->vertices, &views->indices, &views->meshlets, &views->meshlet_vertices, &views->meshlet_triangles,
				&views->meshes, &views->instances, &views->materials, &views->tlas };
			std::copy(std::begin(v), std::end(v), scene_views);
		}
		for (sg::u32 i = 0; i < 9; i++)
		{
			b.set_srv(scene_views[i] ? *scene_views[i] : dummy_srv_buffer.srv, i);
		}
		b.set_srv(binding.sky ? sky.srv : dummy_texture.srv, 9);
		for (sg::u32 i = 0; i < binding.inputs.size(); i++)
		{
			b.set_srv(binding.inputs[i] ? *binding.inputs[i] : dummy_texture.srv, 10 + i);
		}
		for (sg::u32 i = 0; i < binding.outputs.size(); i++)
		{
			b.set_uav(binding.outputs[i] ? *binding.outputs[i] : dummy_uav_buffer.uav, i);
		}
		command_list->bind(b, type);
	}

	// Changing pipelines changes the root signature, which invalidates bindings: every dispatch binds again
	void Renderer::dispatch(sg::Pipeline& pipeline, sg::ConstantBufferView pass_constants, const Binding& binding, sg::u32 groups_x, sg::u32 groups_y)
	{
		command_list->set_pipeline(&pipeline);
		bind(sg::PipelineType::Compute, pass_constants, binding);
		command_list->dispatch(groups_x, groups_y, 1);
	}

	void Renderer::dispatch_screen(sg::Pipeline& pipeline, sg::ConstantBufferView pass_constants, const Binding& binding)
	{
		dispatch(pipeline, pass_constants, binding, groups(width, 8), groups(height, 8));
	}

	void Renderer::clear(BufferResource& buffer, sg::u32 value)
	{
		sg::ShaderResourceView no_srv = {};
		command_list->clear_buffer_uint(buffer.uav, no_srv, value);
	}

	Renderer::ScopedTimer Renderer::timed(const char* pass_name)
	{
		if (timers.size() >= kMaxTimers)
		{
			return { *this, false };
		}
		timers.push_back({ pass_name, timestamp_pool->allocate_new_timestamp() });
		timestamp_pool->begin_timestamp(timers.back().index, command_list);
		return { *this, true };
	}

	void Renderer::end_timer()
	{
		timestamp_pool->end_timestamp(timers.back().index, command_list);
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// Passes

	void Renderer::render_sky()
	{
		const ScopedTimer timer = timed("Sky");
		const float e = cv_sun_illuminance;
		const gpu::SkyConstants sc = { { e, e, e }, cv_sky_mie_g, cv_sky_rayleigh, cv_sky_mie, cv_sky_ozone, cv_sky_ground, { sky_width, sky_height }, {} };
		const sg::ConstantBufferView cbv = constants(sc);
		// Higher order scattering LUT, then the sky (these passes write the sky, so it is not bound at t9)
		Binding b;
		b.sky = false;
		b.outputs[1] = &sky_multiple_scattering.uav;
		dispatch(*pipelines.sky_multiple_scattering, cbv, b, groups(32, 8), groups(32, 8));
		b = Binding();
		b.sky = false;
		b.inputs[0] = &sky_multiple_scattering.srv;
		b.outputs[0] = &sky.uav;
		dispatch(*pipelines.sky, cbv, b, groups(sky_width, 8), groups(sky_height, 8));
	}

	void Renderer::render_geometry_phase(sg::u32 phase)
	{
		clear(task_counts);
		gpu::GeometryPassConstants gc = {};
		gc.phase = phase;
		gc.instance_count = scene->get_instance_count();
		gc.task_region_offsets = { task_region_offsets[0], task_region_offsets[1], task_region_offsets[2], task_region_offsets[3] };
		gc.task_region_sizes = { task_region_sizes[0], task_region_sizes[1], task_region_sizes[2], task_region_sizes[3] };
		Binding b;
		b.inputs[0] = &hzb.srv;
		b.outputs = { &tasks.uav, &task_counts.uav, &draw_arguments.uav, &instance_visibility.uav, &meshlet_visibility.uav, &cull_statistics.uav, nullptr, nullptr };
		const sg::ConstantBufferView cbv = constants(gc);
		dispatch(*pipelines.cull_instances, cbv, b, groups(gc.instance_count, 64));
		dispatch(*pipelines.build_draw_arguments, cbv, b, 1);

		sg::Viewport viewport;
		viewport.width = static_cast<float>(width);
		viewport.height = static_cast<float>(height);
		sg::ScissorRect scissor;
		scissor.right = width;
		scissor.bottom = height;
		command_list->start_geometry_pass(1, &visibility.rtv, viewport, scissor, depth.dsv);
		if (phase == PBR_PHASE_EARLY)
		{
			command_list->clear_render_target_view(visibility.rtv, { 0.0f, 0.0f, 0.0f, 0.0f });
		}
		indirect_arguments->set_indirect_buffer(draw_arguments.buffer);
		for (sg::u32 bucket = 0; bucket < PBR_BUCKET_COUNT; bucket++)
		{
			gc.bucket = bucket;
			command_list->set_pipeline(pipelines.visibility[bucket].get());
			bind(sg::PipelineType::Geometry, constants(gc), b);
			command_list->execute_indirect(*indirect_arguments, bucket * sizeof(gpu::uint4));
		}
		command_list->end_geometry_pass();
	}

	void Renderer::build_hzb()
	{
		for (sg::u32 level = 0; level < hzb_mips; level++)
		{
			gpu::GeometryPassConstants gc = {};
			gc.hzb_level = level;
			Binding b;
			b.inputs[1] = &depth.srv;
			b.outputs[6] = level > 0 ? &hzb_mip_uavs[level - 1] : nullptr;
			b.outputs[7] = &hzb_mip_uavs[level];
			dispatch(level == 0 ? *pipelines.hzb_first : *pipelines.hzb_down, constants(gc), b, groups(std::max(hzb_width >> level, 1u), 8), groups(std::max(hzb_height >> level, 1u), 8));
		}
	}

	void Renderer::render_geometry()
	{
		clear(cull_statistics);
		command_list->clear_depth_stencil_view(depth.dsv, true, false, 0.0f); // Reverse Z: 0 is the far plane
		{
			const ScopedTimer timer = timed("Geometry (early)");
			render_geometry_phase(PBR_PHASE_EARLY);
		}
		if (cv_cull_occlusion)
		{
			{
				const ScopedTimer timer = timed("HZB");
				build_hzb();
			}
			const ScopedTimer timer = timed("Geometry (late)");
			render_geometry_phase(PBR_PHASE_LATE);
		}
		else if (cv_debug_view == PBR_DEBUG_HZB)
		{
			build_hzb();
		}

		// Visibility buffer -> G-buffer (no pass constants)
		const ScopedTimer timer = timed("Resolve");
		Binding b;
		b.inputs[0] = &visibility.srv;
		b.outputs = { &gbuffer_albedo.uav, &gbuffer_normal.uav, &gbuffer_geometry.uav, &gbuffer_emissive.uav, &motion.uav, &guide[current].uav, nullptr, nullptr };
		dispatch_screen(*pipelines.resolve, frame_cbv, b);
	}

	Renderer::Target& Renderer::denoise(DenoiseTargets& t, Target& raw, sg::u32 signal, int iterations, bool enabled)
	{
		if (!enabled)
		{
			return raw;
		}
		const sg::u32 previous = current ^ 1;
		// Unfiltered history: the temporal pass accumulates raw samples (an unbiased running mean) and the spatial filter only
		// affects this frame's output. Filtered history (SVGF): the first a-trous iteration's result is next frame's history.
		const bool filtered_history = cv_denoise_filtered_history;
		Target& accumulated = filtered_history ? t.accumulated : t.history[current];
		gpu::DenoiseConstants dc = {};
		dc.signal = signal;
		dc.step = 1;
		dc.max_history = history_valid ? static_cast<sg::u32>(cv_denoise_history.get()) : 1;
		dc.colour_blend = cv_denoise_alpha;
		dc.moments_blend = cv_denoise_alpha_moments;
		dc.phi_colour = cv_denoise_phi_colour;
		dc.phi_normal = cv_denoise_phi_normal;
		dc.phi_depth = cv_denoise_phi_depth;
		dc.spatial_variance = cv_denoise_spatial_variance;

		Binding b;
		b.inputs = { &raw.srv, &t.history[previous].srv, &t.moments[previous].srv, &motion.srv, &guide[current].srv, &guide[previous].srv };
		b.outputs[0] = &accumulated.uav;
		b.outputs[1] = &t.moments[current].uav;
		dispatch_screen(*pipelines.denoise_temporal, constants(dc), b);

		b = Binding();
		b.inputs[0] = &accumulated.srv;
		b.inputs[2] = &t.moments[current].srv;
		b.inputs[4] = &guide[current].srv;
		b.outputs[0] = &t.ping.uav;
		dispatch_screen(*pipelines.denoise_variance, constants(dc), b);

		Target* source = &t.ping;
		Target* destination = &t.pong;
		for (int i = 0; i < iterations; i++, std::swap(source, destination))
		{
			dc.step = 1u << i;
			dc.write_history = i == 0 && filtered_history;
			b = Binding();
			b.inputs[0] = &source->srv;
			b.inputs[4] = &guide[current].srv;
			b.outputs[0] = &destination->uav;
			b.outputs[1] = dc.write_history ? &t.history[current].uav : nullptr;
			dispatch_screen(*pipelines.denoise_atrous, constants(dc), b);
		}
		return *source;
	}

	void Renderer::render_lighting()
	{
		{
			const ScopedTimer timer = timed("Ray Traced Lighting");
			gpu::LightingConstants lc = {};
			lc.diffuse_cone_spread = cv_diffuse_spread;
			lc.specular_cone_spread = cv_specular_spread;
			lc.pixel_spread = 2.0f / (frame.projection._22 * height); // The angle a pixel covers at the image centre
			lc.specular_max_roughness = cv_specular_max_roughness;
			lc.emissive_candidates_at_hits = static_cast<sg::u32>(cv_emissive_candidates_secondary.get());
			lc.options = (cv_emissive_at_hits ? PBR_LIGHTING_EMISSIVE_AT_HITS : 0u) | (cv_sun_shadows_at_hits ? PBR_LIGHTING_SUN_SHADOWS_AT_HITS : 0u) |
				(cv_cache_propagation ? PBR_LIGHTING_CACHE_PROPAGATION : 0u);
			lc.cache_update_interval = static_cast<sg::u32>(cv_cache_update_interval.get());
			const sg::ConstantBufferView cbv = constants(lc);
			Binding b;
			b.inputs = { &gbuffer_albedo.srv, &gbuffer_normal.srv, &gbuffer_geometry.srv, &depth.srv, nullptr, &scene->get_views().emissive_triangles };
			b.outputs = { &shadow_raw.uav, &diffuse_raw.uav, &specular_raw.uav, &cache_keys.uav, &cache_accumulators.uav, &cache_values.uav, nullptr, nullptr };
			dispatch_screen(*pipelines.lighting, cbv, b);
			if (cv_radiance_cache)
			{
				dispatch(*pipelines.cache_resolve, cbv, b, groups(cache_capacity, 64));
			}
		}
		Target* shadow = nullptr;
		Target* diffuse = nullptr;
		Target* specular = nullptr;
		{
			const ScopedTimer timer = timed("Denoise Shadow");
			shadow = &denoise(shadow_denoise, shadow_raw, PBR_SIGNAL_SHADOW, cv_denoise_shadow_iterations, cv_denoise_shadow);
		}
		{
			const ScopedTimer timer = timed("Denoise Diffuse");
			diffuse = &denoise(diffuse_denoise, diffuse_raw, PBR_SIGNAL_DIFFUSE, cv_denoise_diffuse_iterations, cv_denoise_diffuse);
		}
		{
			const ScopedTimer timer = timed("Denoise Specular");
			specular = &denoise(specular_denoise, specular_raw, PBR_SIGNAL_SPECULAR, cv_denoise_specular_iterations, cv_denoise_specular);
		}
		render_composite(*shadow, *diffuse, *specular);
	}

	void Renderer::render_composite(const Target& shadow, const Target& diffuse, const Target& specular)
	{
		const ScopedTimer timer = timed("Composite");
		const gpu::CompositeConstants cc =
		{
			gbuffer_albedo.bindless, gbuffer_normal.bindless, gbuffer_geometry.bindless, gbuffer_emissive.bindless,
			depth.bindless, shadow.bindless, diffuse.bindless, specular.bindless,
			shadow_raw.bindless, diffuse_raw.bindless, specular_raw.bindless, motion.bindless,
			diffuse_denoise.moments[current].bindless, hzb.bindless, visibility.bindless, 0,
		};
		Binding b;
		b.outputs[0] = &lit.uav;
		dispatch_screen(*pipelines.composite, constants(cc), b);
	}

	Renderer::Target& Renderer::render_taa()
	{
		const ScopedTimer timer = timed("TAA");
		const gpu::TAAConstants tc = { lit.bindless, taa_history[current ^ 1].bindless, motion.bindless, depth.bindless, history_valid && cv_taa ? 1u : 0u, cv_taa_blend, cv_taa_gamma, 0 };
		Binding b;
		b.outputs[0] = &taa_history[current].uav;
		dispatch_screen(*pipelines.taa, constants(tc), b);
		return taa_history[current];
	}

	Renderer::Target& Renderer::render_path_tracer()
	{
		const ScopedTimer timer = timed("Path Tracer");
		const sg::u32 max_frames = static_cast<sg::u32>(cv_pt_max_frames.get());
		const gpu::PathTraceConstants pc = { static_cast<sg::u32>(cv_pt_bounces.get()), static_cast<sg::u32>(cv_pt_rr.get()), accumulated_frames, max_frames, 2.0f / (frame.projection._22 * height), {} };
		Binding b;
		b.inputs[5] = &scene->get_views().emissive_triangles;
		b.outputs[0] = &path_accumulation.uav;
		b.outputs[1] = &path_output.uav;
		dispatch_screen(*pipelines.path_trace, constants(pc), b);
		accumulated_frames = std::min(accumulated_frames + 1, max_frames);
		return path_output;
	}

	// Exposure histogram (read back for the next frame's automatic exposure) and tone mapping into the output. Without an
	// image (while loading) the sky placeholder shows.
	void Renderer::render_tone_map(const Target* hdr, sg::UnorderedAccessView& output)
	{
		const ScopedTimer timer = timed("Exposure + Tone Map");
		const gpu::ToneMapConstants tc = { hdr ? hdr->bindless : dummy_texture.bindless, static_cast<sg::u32>(cv_tonemap.get()), cv_dither, 0 };
		const sg::ConstantBufferView cbv = constants(tc);
		Binding b;
		b.outputs[1] = &histogram.uav;
		b.outputs[2] = &output;
		if (hdr)
		{
			clear(histogram);
			dispatch(*pipelines.histogram, cbv, b, groups(width, 16), groups(height, 16));
		}
		dispatch_screen(*pipelines.tonemap, cbv, b);
		bind(sg::PipelineType::Compute, cbv, Binding()); // Releases the output UAV (the application's render target)
		if (hdr)
		{
			command_list->copy_buffer_to_buffer(histogram_readback.get(), histogram.buffer.get());
			command_list->copy_buffer_to_buffer(statistics_readback.get(), cull_statistics.buffer.get());
		}
		readback_valid = hdr != nullptr;
	}

	void Renderer::render(sg::CommandList& command_list_, const Camera& camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer_, sg::UnorderedAccessView& output)
	{
		if (!supported)
		{
			return;
		}
		command_list = &command_list_;
		cbuffer = &cbuffer_;
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
		scene->update(*command_list, upload_heap, *cbuffer);
		if (!was_ready && scene->is_ready())
		{
			if (camera_on_ready)
			{
				apply_preset_camera(*camera_on_ready);
				camera_on_ready = nullptr;
			}
			create_scene_resources();
		}

		update_frame_constants(camera);
		if (history_reset_pending)
		{
			history_reset_pending = false;
			history_valid = false;
			accumulated_frames = 0;
			clear(cache_keys);
			clear(cache_accumulators);
			sg::ShaderResourceView no_srv = {};
			command_list->clear_buffer_float(cache_values.uav, no_srv, 1000.0f); // CACHE_NO_VALUE
		}
		frame_cbv = constants(frame);

		render_sky();
		const Target* hdr = nullptr;
		if (scene->is_ready())
		{
			render_geometry();
			if (cv_mode == 1)
			{
				hdr = &render_path_tracer();
			}
			else
			{
				render_lighting();
				hdr = &render_taa();
			}
		}
		render_tone_map(hdr, output);
		timestamp_pool->end_frame(command_list);
		frame_recorded = true;

		history_valid = scene->is_ready();
		current ^= 1;
		frame_index++;
		command_list = nullptr;
		cbuffer = nullptr;
	}
}
