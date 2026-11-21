// Ray traced lighting at one sample per pixel, denoised afterwards:
//   - sun shadow: a ray towards a random point of the sun's disk (soft shadows of the right width)
//   - diffuse indirect: a cosine distributed ray; its hit is lit by the sun (with a shadow ray), emissive surfaces, the
//     sky and the radiance cache (further bounces)
//   - specular: a GGX visible normal sampled reflection ray, shaded like the diffuse hits
// Outputs are demodulated (divided by the surface's reflectance) so the denoisers blur lighting, not texture detail, and
// pre-exposed (multiplied by the exposure) so half floats hold them and the firefly clamp works in display units.
//
// Pass constants: f0 = (diffuse ray cone spread, specular cone spread scale, pixel spread angle, specular max roughness)
//                 u0 = (emissive light candidates at secondary hits, LIGHTING_* options, cache update interval, unused)

#include "pbrSurface.hlsli"
#include "pbrRadianceCache.hlsli"

cbuffer LightingConstants : register(b1)
{
    PassConstants pass;
};

Texture2D<float4> GBufferAlbedoMetallic : register(t10);
Texture2D<float4> GBufferNormalRoughness : register(t11);
Texture2D<float4> GBufferGeometry : register(t12);
Texture2D<float> Depth : register(t13);

// Real time lighting options (pass u0.y)
#define LIGHTING_EMISSIVE_AT_HITS 1u        // Emissive light samples at indirect ray hits (else rays find emitters themselves)
#define LIGHTING_SUN_SHADOWS_AT_HITS 2u     // Shadow rays for sunlight at indirect ray hits
#define LIGHTING_CACHE_PROPAGATION 4u       // Rays from diffuse ray hits that feed the radiance cache (one more bounce)

bool HasLightingOption(uint option)
{
    return (pass.u0.y & option) != 0;
}

// Whether ray hits sample emissive lights (otherwise their emission counts where the rays find it)
bool EmissiveAtHits()
{
    return EmissiveSamplingEnabled() && HasLightingOption(LIGHTING_EMISSIVE_AT_HITS);
}

RWTexture2D<float> ShadowOutput : register(u0);             // Sun visibility
RWTexture2D<float4> DiffuseOutput : register(u1);           // Incoming radiance (irradiance / pi estimate), hit distance
RWTexture2D<float4> SpecularOutput : register(u2);          // Reflected radiance / specular albedo, hit distance

// Radiance leaving a secondary hit towards -direction (g and s: the hit's surface): direct sunlight, one emissive light
// sample and the radiance cache's indirect light. The hit's own emission is left out where emissive light sampling of the
// ray's origin accounts for it.
// The radiance cache holds indirect light only (no direct sun or emissive light), so it adds to the direct light here.
float3 ShadeSecondaryHit(RayHit hit, float3 direction, float cone_width, inout RNG rng, out SurfaceGeometry g, out MaterialSample s, bool include_emission)
{
    ResolveHit(hit, direction, cone_width, g, s);
    float3 radiance = include_emission ? s.emissive : 0.0f;
    radiance += DirectSun(g, s, -direction, rng.Next2(), HasFlag(PBR_FLAG_SHADOWS) && HasLightingOption(LIGHTING_SUN_SHADOWS_AT_HITS), pass.f0.x, frame.alpha_test_limits.y);
    if (EmissiveAtHits())
    {
        float3 light_diffuse, light_specular, irradiance, l;
        float pdf;
        if (EmissiveLight(g, s, -direction, rng, pass.u0.x, frame.alpha_test_limits.y, light_diffuse, light_specular, irradiance, pdf, l))
        {
            radiance += light_diffuse + light_specular;
        }
    }
    // Light that reached the hit point over further bounces
    if (HasFlag(PBR_FLAG_RADIANCE_CACHE))
    {
        float3 irradiance;
        if (RadianceCacheLookup(g.position, g.geometric_normal, hit.t, irradiance))
        {
            radiance += DiffuseColour(s) * irradiance * INV_PI * s.ao;
        }
    }
    return radiance;
}

float3 MissRadiance(float3 direction)
{
    return HasFlag(PBR_FLAG_SKY_LIGHTING) ? SampleSky(direction) : 0.0f;
}

// Clamps an exposed sample's luminance
float3 ClampFirefly(float3 c)
{
    float l = Luminance(c);
    return frame.firefly_clamp > 0.0f && l > frame.firefly_clamp ? c * (frame.firefly_clamp / l) : c;
}

[numthreads(8, 8, 1)]
void lighting(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= (uint2)frame.resolution))
    {
        return;
    }
    float depth = Depth[pixel];
    if (depth <= 0.0f)
    {
        ShadowOutput[pixel] = 0.0f;
        DiffuseOutput[pixel] = 0.0f;
        SpecularOutput[pixel] = 0.0f;
        return;
    }
    float2 uv = (pixel + 0.5f) * frame.inverse_resolution;
    float3 position = ReconstructWorldPosition(uv, depth);
    float4 albedo_metallic = GBufferAlbedoMetallic[pixel];
    float4 normal_roughness = GBufferNormalRoughness[pixel];
    float4 geometry = GBufferGeometry[pixel];

    MaterialSample s;
    s.albedo = albedo_metallic.rgb;
    s.metallic = albedo_metallic.a;
    s.normal = normalize(normal_roughness.xyz);
    s.roughness = normal_roughness.w;
    s.emissive = 0.0f;
    s.ao = geometry.z;
    s.opacity = 1.0f;
    float3 geometric_normal = DecodeOctahedral(geometry.xy);
    float3 v = normalize(frame.camera_position.xyz - position);
    float3 origin = OffsetRayOrigin(position, geometric_normal);
    float distance_to_camera = length(frame.camera_position.xyz - position);
    float pixel_cone = pass.f0.z * distance_to_camera;
    RNG rng = CreateRNG(pixel, frame.frame_index, 0);

    // Sun visibility
    float shadow = 0.0f;
    float3 sun_l = ToWorld(SampleCone(rng.Next2(), frame.sun_direction.w), frame.sun_direction.xyz);
    if (dot(geometric_normal, frame.sun_direction.xyz) > -0.05f)
    {
        shadow = !HasFlag(PBR_FLAG_SHADOWS) || TraceVisibility(origin, sun_l, frame.max_ray_distance, pass.f0.z, frame.alpha_test_limits.x) ? 1.0f : 0.0f;
    }
    ShadowOutput[pixel] = shadow;

    // Rough surfaces may reuse the diffuse ray for their reflections (pbr.light.specular_max_roughness): a wide lobe sees
    // nearly the same incoming light as the cosine lobe, and one ray fewer per pixel is a large saving
    bool reuse_diffuse_ray = HasFlag(PBR_FLAG_DIFFUSE_GI) && s.roughness > pass.f0.w;

    // Diffuse indirect
    float4 diffuse = 0.0f;
    if (HasFlag(PBR_FLAG_DIFFUSE_GI))
    {
        float3 l = ToWorld(SampleCosineHemisphere(rng.Next2()), s.normal);
        if (dot(l, geometric_normal) > 0.0f)
        {
            RayHit hit = TraceClosest(origin, l, frame.max_ray_distance, pass.f0.x, frame.alpha_test_limits.y);
            SurfaceGeometry hit_surface;
            MaterialSample hit_material;
            // The emissive light sample at this pixel (below) covers emitters this ray hits
            float3 radiance = hit.hit ? ShadeSecondaryHit(hit, l, pixel_cone + pass.f0.x * hit.t, rng, hit_surface, hit_material, !EmissiveSamplingEnabled()) : MissRadiance(l);
            diffuse = float4(ClampFirefly(radiance * frame.exposure), hit.t);

            // The hit's own irradiance (one more bounce) feeds its cache cell: the cache then also covers surfaces the camera
            // does not see (behind it, around corners), which light reaches the visible ones from
            if (hit.hit && HasFlag(PBR_FLAG_RADIANCE_CACHE) && HasLightingOption(LIGHTING_CACHE_PROPAGATION) && RadianceCacheUpdatePixel(pixel, frame.frame_index, pass.u0.z))
            {
                float3 l2 = ToWorld(SampleCosineHemisphere(rng.Next2()), hit_material.normal);
                if (dot(l2, hit_surface.geometric_normal) > 0.0f)
                {
                    RayHit hit2 = TraceClosest(OffsetRayOrigin(hit_surface.position, hit_surface.geometric_normal), l2, frame.max_ray_distance, pass.f0.x, frame.alpha_test_limits.y);
                    SurfaceGeometry g2;
                    MaterialSample s2;
                    float3 radiance2 = hit2.hit ? ShadeSecondaryHit(hit2, l2, pass.f0.x * (hit.t + hit2.t), rng, g2, s2, !EmissiveAtHits()) : MissRadiance(l2);
                    float exposed = Luminance(radiance2) * frame.exposure;
                    radiance2 = frame.firefly_clamp > 0.0f && exposed > frame.firefly_clamp ? radiance2 * (frame.firefly_clamp / exposed) : radiance2;
                    RadianceCacheAccumulate(hit_surface.position, hit_surface.geometric_normal, 0.0f, radiance2 * PI, pixel, frame.frame_index, pass.u0.z);
                }
            }
        }
        else
        {
            diffuse = float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }
    // The cache gets the indirect light only (hits add direct sun and emissive light themselves)
    float3 indirect_irradiance = diffuse.rgb / max(frame.exposure, 1e-20f) * PI;
    float4 diffuse_ray = diffuse;

    // Emissive surfaces: one light sample (diffuse rays leave their emission out). Its diffuse part goes to the demodulated
    // diffuse signal as an incoming radiance estimate (E / pi = L cos / (pdf pi)); its specular part to the specular signal,
    // weighted against reflection rays that hit emitters (multiple importance sampling, power heuristic).
    float3 emissive_specular = 0.0f;
    if (EmissiveSamplingEnabled() && (HasFlag(PBR_FLAG_DIFFUSE_GI) || HasFlag(PBR_FLAG_SPECULAR)))
    {
        SurfaceGeometry g;
        g.position = position;
        g.geometric_normal = geometric_normal;
        g.normal = s.normal;
        g.tangent = 0.0f;
        g.uv = 0.0f;
        g.front_face = true;
        g.uv_area_ratio = 0.0f;
        float3 light_diffuse, light_specular, irradiance, l;
        float pdf;
        if (EmissiveLight(g, s, v, rng, EmissiveCandidates(), frame.alpha_test_limits.x, light_diffuse, light_specular, irradiance, pdf, l))
        {
            // Direct light: not clamped (the firefly clamp is for indirect paths)
            if (HasFlag(PBR_FLAG_DIFFUSE_GI))
            {
                diffuse.rgb += irradiance * INV_PI * frame.exposure;
            }
            if (HasFlag(PBR_FLAG_SPECULAR))
            {
                // Without a reflection ray the light sample is the only strategy for emitters (weight 1)
                float pdf_reflection = reuse_diffuse_ray ? 0.0f : BRDFPdf(s, v, l, 1.0f);
                float w = pdf * pdf / max(pdf * pdf + pdf_reflection * pdf_reflection, 1e-20f);
                float3 specular_albedo = SpecularAlbedo(SpecularF0(s), s.roughness, max(dot(s.normal, v), 1e-4f));
                emissive_specular = light_specular * w / max(specular_albedo, 1e-4f) * frame.exposure;
            }
        }
    }
    DiffuseOutput[pixel] = diffuse;

    // Specular: VNDF sampled reflection, weighted by F * G2 / G1 and divided by the lobe's directional albedo
    float4 specular = 0.0f;
    if (HasFlag(PBR_FLAG_SPECULAR) && reuse_diffuse_ray)
    {
        specular = diffuse_ray;
    }
    else if (HasFlag(PBR_FLAG_SPECULAR))
    {
        float alpha = s.roughness * s.roughness;
        float3 v_local = ToLocal(v, s.normal);
        float n_dot_v = max(v_local.z, 1e-4f);
        float3 h = ToWorld(SampleGGXVNDF(float3(v_local.xy, n_dot_v), alpha, rng.Next2()), s.normal);
        float3 l = reflect(-v, h);
        float n_dot_l = dot(s.normal, l);
        if (n_dot_l > 0.0f && dot(l, geometric_normal) > 0.0f)
        {
            float3 f0 = SpecularF0(s);
            float3 f = FresnelSchlick(f0, saturate(dot(v, h)));
            float g2_over_g1 = (4.0f * n_dot_l * n_dot_v * V_SmithGGXCorrelated(n_dot_v, n_dot_l, alpha)) / SmithG1(n_dot_v, alpha);
            float3 weight = f * g2_over_g1 / max(SpecularAlbedo(f0, s.roughness, n_dot_v), 1e-4f);
            float cone_spread = pass.f0.y * alpha + pass.f0.z;
            RayHit hit = TraceClosest(origin, l, frame.max_ray_distance, cone_spread, frame.alpha_test_limits.y);
            SurfaceGeometry hit_surface;
            MaterialSample hit_material;
            bool sampled = EmissiveSamplingEnabled();
            float3 radiance = hit.hit ? ShadeSecondaryHit(hit, l, pixel_cone + cone_spread * hit.t, rng, hit_surface, hit_material, !sampled) : MissRadiance(l) + SunDisk(l) * (alpha < 0.0025f ? 1.0f : 0.0f);
            if (hit.hit && sampled && any(hit_material.emissive > 0.0f))
            {
                // Emitters found by the reflection ray, weighted against the emissive light sample
                Material material = Materials[Meshes[Instances[hit.instance].mesh].material];
                float pdf_light = EmissiveAreaPdf(material) * hit.t * hit.t / max(abs(dot(hit_surface.geometric_normal, l)), 1e-4f);
                float pdf_reflection = BRDFPdf(s, v, l, 1.0f);
                radiance += hit_material.emissive * pdf_reflection * pdf_reflection / max(pdf_reflection * pdf_reflection + pdf_light * pdf_light, 1e-20f);
            }
            specular = float4(ClampFirefly(radiance * weight * frame.exposure), hit.t);
        }
    }
    specular.rgb += emissive_specular;
    SpecularOutput[pixel] = specular;

    // Feeds the radiance cache with this pixel's irradiance estimate (cosine sampling: E = pi * mean radiance)
    if (HasFlag(PBR_FLAG_RADIANCE_CACHE) && HasFlag(PBR_FLAG_DIFFUSE_GI))
    {
        RadianceCacheAccumulate(position, geometric_normal, distance_to_camera, indirect_irradiance, pixel, frame.frame_index, pass.u0.z);
    }
}

// Blends each cache cell's accumulated samples of this frame into its value; cells without samples for a while are evicted
[numthreads(64, 1, 1)]
void radiance_cache_resolve(uint3 dtid : SV_DispatchThreadID)
{
    uint i = dtid.x;
    if (i >= (uint)frame.radiance_cache.w || CacheKeys[i] == 0)
    {
        return;
    }
    uint4 accumulated = CacheAccumulators[i];
    float4 value = CacheValues[i];
    if (accumulated.w > 0)
    {
        float3 irradiance = (float3)accumulated.rgb / (accumulated.w * frame.exposure * CACHE_FIXED_POINT);
        value.rgb = value.w >= 1000.0f ? irradiance : lerp(value.rgb, irradiance, frame.radiance_cache.z);
        value.w = 0.0f;
        CacheAccumulators[i] = 0;
    }
    else
    {
        value.w += 1.0f;
        if (value.w > 120.0f && value.w < 1000.0f)
        {
            CacheKeys[i] = 0;
            value = float4(0.0f, 0.0f, 0.0f, 1000.0f);
        }
    }
    CacheValues[i] = value;
}
