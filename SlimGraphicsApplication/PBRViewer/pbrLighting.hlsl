// Ray traced lighting at one sample per pixel, denoised afterwards:
//   - sun shadow: a ray towards a random point of the sun's disk (soft shadows of the right width)
//   - diffuse indirect: a cosine distributed ray; its hit is lit by the sun, the sky and the radiance cache (further bounces)
//   - specular: a GGX visible normal sampled reflection ray, shaded like the diffuse hits
//   - emissive surfaces: one resampled light sample, its diffuse part added to the diffuse signal and its specular part
//     weighted against reflection rays that hit emitters
// Outputs are demodulated (divided by the surface's reflectance) so the denoisers blur lighting, not texture detail, and
// pre-exposed (multiplied by the exposure) so half floats hold them and the firefly clamp works in display units.

#include "pbrSurface.hlsli"
#include "pbrRadianceCache.hlsli"

ConstantBuffer<LightingConstants> pass : register(b1);

Texture2D<float4> GBufferAlbedoMetallic : register(t10);
Texture2D<float4> GBufferNormalRoughness : register(t11);
Texture2D<float4> GBufferGeometry : register(t12);
Texture2D<float> Depth : register(t13);

RWTexture2D<float> ShadowOutput : register(u0);             // Sun visibility
RWTexture2D<float4> DiffuseOutput : register(u1);           // Incoming radiance (irradiance / pi estimate), hit distance
RWTexture2D<float4> SpecularOutput : register(u2);          // Reflected radiance / specular albedo, hit distance

bool HasLightingOption(uint option)
{
    return (pass.options & option) != 0;
}

// Whether ray hits sample emissive lights (otherwise their emission counts where the rays find it)
bool EmissiveAtHits()
{
    return EmissiveSamplingEnabled() && HasLightingOption(PBR_LIGHTING_EMISSIVE_AT_HITS);
}

// Radiance leaving a ray's hit towards -direction (g and s: the hit's surface): direct sunlight, an emissive light sample
// (EmissiveAtHits) and the radiance cache's indirect light. The hit's own emission is left out when 'origin_samples_lights':
// the light sample at the ray's origin accounts for it. The cache holds indirect light only, so it adds to the direct light.
float3 ShadeHit(RayHit hit, float3 direction, float cone_width, bool origin_samples_lights, inout RNG rng, out SurfaceGeometry g, out MaterialSample s)
{
    ResolveHit(hit, direction, cone_width, g, s);
    float3 v = -direction;
    float3 radiance = origin_samples_lights ? 0.0f : s.emissive;
    bool shadows = HasFlag(PBR_FLAG_SHADOWS) && HasLightingOption(PBR_LIGHTING_SUN_SHADOWS_AT_HITS);
    radiance += DirectSun(g, s, v, rng.Next2(), shadows, pass.diffuse_cone_spread, frame.alpha_tests_indirect);
    EmissiveLightSample light;
    if (EmissiveAtHits() && SampleEmissiveLight(g, s, v, rng, pass.emissive_candidates_at_hits, frame.alpha_tests_indirect, light))
    {
        radiance += light.diffuse + light.specular;
    }
    float3 irradiance;
    if (HasFlag(PBR_FLAG_RADIANCE_CACHE) && RadianceCacheLookup(g.position, g.geometric_normal, irradiance))
    {
        radiance += DiffuseColour(s) * irradiance * INV_PI * s.ao;
    }
    return radiance;
}

// Radiance arriving along a ray (its hit's, see ShadeHit, or the sky's). cone_width is the ray cone's width at the origin.
float3 TraceRadiance(float3 origin, float3 direction, float cone_width, float cone_spread, bool origin_samples_lights, inout RNG rng, out RayHit hit, out SurfaceGeometry g, out MaterialSample s)
{
    hit = TraceClosest(origin, direction, frame.max_ray_distance, cone_spread, frame.alpha_tests_indirect);
    g = (SurfaceGeometry)0;
    s = (MaterialSample)0;
    return hit.hit ? ShadeHit(hit, direction, cone_width + cone_spread * hit.t, origin_samples_lights, rng, g, s) : MissRadiance(direction, false);
}

// One more bounce from a diffuse ray's hit into the hit's cache cell: the cache then also covers surfaces the camera does
// not see (behind it, around corners), which light reaches the visible ones from
void PropagateRadianceCache(SurfaceGeometry hit_surface, MaterialSample hit_material, float hit_distance, uint2 pixel, inout RNG rng)
{
    float3 l = ToWorld(SampleCosineHemisphere(rng.Next2()), hit_material.normal);
    if (dot(l, hit_surface.geometric_normal) <= 0.0f)
    {
        return;
    }
    RayHit hit;
    SurfaceGeometry g;
    MaterialSample s;
    float3 origin = OffsetRayOrigin(hit_surface.position, hit_surface.geometric_normal);
    float3 radiance = TraceRadiance(origin, l, pass.diffuse_cone_spread * hit_distance, pass.diffuse_cone_spread, EmissiveAtHits(), rng, hit, g, s);
    RadianceCacheAccumulate(hit_surface.position, hit_surface.geometric_normal, ClampFirefly(radiance) * PI, pixel);
}

[numthreads(8, 8, 1)]
void lighting(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (!IsOnScreen(pixel))
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
    float4 geometry = GBufferGeometry[pixel];
    MaterialSample s = GBufferMaterial(GBufferAlbedoMetallic[pixel], GBufferNormalRoughness[pixel], geometry.z, 0.0f);
    SurfaceGeometry g = (SurfaceGeometry)0;
    g.position = ReconstructWorldPosition(PixelCentreUV(pixel), depth);
    g.geometric_normal = DecodeOctahedral(geometry.xy);
    g.normal = s.normal;
    float3 v = normalize(frame.camera_position.xyz - g.position);
    float3 origin = OffsetRayOrigin(g.position, g.geometric_normal);
    float pixel_cone = pass.pixel_spread * length(frame.camera_position.xyz - g.position);
    bool sample_lights = EmissiveSamplingEnabled();
    bool update_cache = HasFlag(PBR_FLAG_RADIANCE_CACHE) && RadianceCacheUpdatePixel(pixel, pass.cache_update_interval);
    RNG rng = CreateRNG(pixel, frame.frame_index, 0);

    // Sun visibility
    float shadow = 0.0f;
    float3 sun_l = ToWorld(SampleCone(rng.Next2(), frame.sun_direction.w), frame.sun_direction.xyz);
    if (dot(g.geometric_normal, frame.sun_direction.xyz) > -0.05f)
    {
        shadow = !HasFlag(PBR_FLAG_SHADOWS) || TraceVisibility(origin, sun_l, frame.max_ray_distance, pass.pixel_spread, frame.alpha_tests_shadow) ? 1.0f : 0.0f;
    }
    ShadowOutput[pixel] = shadow;

    // Diffuse indirect
    float4 diffuse = 0.0f;
    if (HasFlag(PBR_FLAG_DIFFUSE_GI))
    {
        float3 l = ToWorld(SampleCosineHemisphere(rng.Next2()), s.normal);
        if (dot(l, g.geometric_normal) > 0.0f)
        {
            RayHit hit;
            SurfaceGeometry hit_surface;
            MaterialSample hit_material;
            float3 radiance = TraceRadiance(origin, l, pixel_cone, pass.diffuse_cone_spread, sample_lights, rng, hit, hit_surface, hit_material);
            diffuse = float4(ClampFirefly(radiance) * frame.exposure, hit.t);
            if (hit.hit && update_cache && HasLightingOption(PBR_LIGHTING_CACHE_PROPAGATION))
            {
                PropagateRadianceCache(hit_surface, hit_material, hit.t, pixel, rng);
            }
        }
        // The cache gets this pixel's indirect irradiance (cosine sampling: E = pi * mean radiance)
        if (update_cache)
        {
            RadianceCacheAccumulate(g.position, g.geometric_normal, diffuse.rgb / frame.exposure * PI, pixel);
        }
    }

    // Rough surfaces may reuse the diffuse ray for their reflections (pbr.light.specular_max_roughness): a wide lobe sees
    // nearly the same incoming light as the cosine lobe, and one ray fewer per pixel is a large saving
    bool reuse_diffuse_ray = HasFlag(PBR_FLAG_DIFFUSE_GI) && s.roughness > pass.specular_max_roughness;
    float3 specular_albedo = max(SpecularAlbedo(s, v), 1e-4f);
    float4 specular = 0.0f;
    if (HasFlag(PBR_FLAG_SPECULAR) && reuse_diffuse_ray)
    {
        specular = diffuse;
    }
    else if (HasFlag(PBR_FLAG_SPECULAR))
    {
        // VNDF sampled reflection, weighted by F * G2 / G1 and divided by the lobe's directional albedo
        float3 l = SampleSpecularDirection(s, v, rng.Next2());
        float n_dot_l = dot(s.normal, l);
        if (n_dot_l > 0.0f && dot(l, g.geometric_normal) > 0.0f)
        {
            float alpha = s.roughness * s.roughness;
            float n_dot_v = NdotV(s, v);
            float3 f = FresnelSchlick(SpecularF0(s), dot(v, normalize(v + l)));
            float3 weight = f * (4.0f * n_dot_l * n_dot_v * V_SmithGGXCorrelated(n_dot_v, n_dot_l, alpha)) / SmithG1(n_dot_v, alpha) / specular_albedo;
            float cone_spread = pass.specular_cone_spread * alpha + pass.pixel_spread;
            RayHit hit;
            SurfaceGeometry hit_surface;
            MaterialSample hit_material;
            float3 radiance = TraceRadiance(origin, l, pixel_cone, cone_spread, sample_lights, rng, hit, hit_surface, hit_material);
            if (!hit.hit && alpha < 0.0025f)
            {
                radiance += SunDisk(l); // Mirrors reflect the sun's disk
            }
            if (hit.hit && sample_lights)
            {
                // Emitters found by the reflection ray, weighted against the light sample below
                radiance += hit_material.emissive * PowerHeuristic(BRDFPdf(s, v, l, 1.0f), EmitterHitPdf(hit, hit_surface, l));
            }
            specular = float4(ClampFirefly(radiance * weight) * frame.exposure, hit.t);
        }
    }

    // Emissive surfaces: one light sample (rays leave the emission they hit to it). Direct light is not clamped.
    EmissiveLightSample light;
    if (sample_lights && (HasFlag(PBR_FLAG_DIFFUSE_GI) || HasFlag(PBR_FLAG_SPECULAR)) && SampleEmissiveLight(g, s, v, rng, frame.emissive_candidates, frame.alpha_tests_shadow, light))
    {
        if (HasFlag(PBR_FLAG_DIFFUSE_GI))
        {
            diffuse.rgb += light.irradiance * INV_PI * frame.exposure;
        }
        if (HasFlag(PBR_FLAG_SPECULAR))
        {
            // Without a reflection ray the light sample is the only strategy for emitters (weight 1)
            float w = PowerHeuristic(light.pdf, reuse_diffuse_ray ? 0.0f : BRDFPdf(s, v, light.direction, 1.0f));
            specular.rgb += light.specular * w / specular_albedo * frame.exposure;
        }
    }

    // The radiance cache debug view: the cache's irradiance at the surface in place of the diffuse signal
    float3 cached;
    if (frame.debug_view == PBR_DEBUG_RADIANCE_CACHE)
    {
        diffuse.rgb = RadianceCacheLookup(g.position, g.geometric_normal, cached) ? cached * INV_PI * frame.exposure : 0.0f;
    }
    DiffuseOutput[pixel] = diffuse;
    SpecularOutput[pixel] = specular;
}
