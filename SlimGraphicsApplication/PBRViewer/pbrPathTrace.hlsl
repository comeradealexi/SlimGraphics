// Reference path tracer: progressive (one path per pixel per frame, averaged until the view or a setting changes), with the
// same materials, BRDF, sun and sky as the real time renderer.
//   - next event estimation of the sun (soft shadow rays over its disk) and of emissive triangles (resampled, like the real
//     time renderer's) at every vertex; emission found by BRDF sampling is weighted against the latter (power heuristic)
//   - BRDF sampling: GGX visible normals for the specular lobe, cosine for diffuse, chosen by their estimated reflectance,
//     weighted by the whole BRDF over the mixture density
//   - the sky is found by BRDF sampling; Russian roulette after a few bounces

#include "pbrSurface.hlsli"

ConstantBuffer<PathTraceConstants> pass : register(b1);

RWTexture2D<float4> Accumulation : register(u0);    // Sum of path radiance (rgb), path count (a)
RWTexture2D<float4> Output : register(u1);          // Average (HDR scene radiance)

// Radiance arriving at the camera along one path through a pixel
float3 TracePath(uint2 pixel, inout RNG rng)
{
    // Box filtered sub pixel position (anti-aliasing converges with the samples)
    float3 origin = frame.camera_position.xyz;
    float3 direction = CameraRayDirection((pixel + rng.Next2()) * frame.inverse_resolution);
    float3 radiance = 0.0f;
    float3 throughput = 1.0f;
    float cone_width = 0.0f;
    float cone_spread = pass.pixel_spread;
    float previous_pdf = 0.0f;  // BRDF sampling density of the direction that reached this vertex
    bool sample_lights = EmissiveSamplingEnabled();
    for (uint bounce = 0; bounce <= pass.max_bounces; bounce++)
    {
        // Indirect contributions are clamped (pbr.light.firefly_clamp, 0 = unbiased)
        RayHit hit = TraceClosest(origin, direction, frame.max_ray_distance, cone_spread, NO_ALPHA_LIMIT);
        if (!hit.hit)
        {
            // The sun's disk is reached by next event estimation; only camera rays see it directly
            float3 sky = MissRadiance(direction, bounce == 0);
            radiance += throughput * (bounce > 0 ? ClampFirefly(sky) : sky);
            break;
        }
        cone_width += cone_spread * hit.t;
        SurfaceGeometry g;
        MaterialSample s;
        ResolveHit(hit, direction, cone_width, g, s);
        float3 v = -direction;

        // Emission found by BRDF sampling (weighted against the light sample), sunlight and one emissive light sample
        bool weigh_emission = bounce > 0 && sample_lights && any(s.emissive > 0.0f);
        float3 vertex_light = s.emissive * (weigh_emission ? PowerHeuristic(previous_pdf, EmitterHitPdf(hit, g, direction)) : 1.0f);
        vertex_light += DirectSun(g, s, v, rng.Next2(), HasFlag(PBR_FLAG_SHADOWS), cone_spread, NO_ALPHA_LIMIT);
        float p_specular = SpecularLobeProbability(s, v);
        EmissiveLightSample light;
        if (sample_lights && SampleEmissiveLight(g, s, v, rng, frame.emissive_candidates, NO_ALPHA_LIMIT, light))
        {
            vertex_light += (light.diffuse + light.specular) * PowerHeuristic(light.pdf, BRDFPdf(s, v, light.direction, p_specular));
        }
        radiance += throughput * (bounce > 0 ? ClampFirefly(vertex_light) : vertex_light);
        if (bounce == pass.max_bounces)
        {
            break;
        }

        // Continue the path: a direction from the specular or diffuse lobe, weighted by the whole BRDF over the mixture's
        // density (cones widen with the lobe)
        float alpha = s.roughness * s.roughness;
        bool specular = rng.Next() < p_specular;
        float3 l = specular ? SampleSpecularDirection(s, v, rng.Next2()) : ToWorld(SampleCosineHemisphere(rng.Next2()), s.normal);
        cone_spread += specular ? alpha * 0.5f : 0.3f;
        float pdf = BRDFPdf(s, v, l, p_specular);
        if (dot(l, g.geometric_normal) <= 0.0f || pdf <= 0.0f)
        {
            break; // Below the surface (shading normals can point there)
        }
        throughput *= BRDF(s, v, l) / pdf;
        previous_pdf = pdf;

        if (bounce >= pass.russian_roulette_start)
        {
            float p = min(max(throughput.r, max(throughput.g, throughput.b)), 0.95f);
            if (rng.Next() > p)
            {
                break;
            }
            throughput /= p;
        }
        origin = OffsetRayOrigin(g.position, g.geometric_normal);
        direction = l;
    }
    // Guard the average against invalid samples
    return any(isnan(radiance)) || any(isinf(radiance)) ? 0.0f : radiance;
}

[numthreads(8, 8, 1)]
void path_trace(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (!IsOnScreen(pixel))
    {
        return;
    }
    float4 sum = pass.accumulated_frames > 0 ? Accumulation[pixel] : 0.0f;
    if (pass.accumulated_frames < pass.max_frames)
    {
        RNG rng = CreateRNG(pixel, pass.accumulated_frames, 7);
        sum += float4(TracePath(pixel, rng), 1.0f);
        Accumulation[pixel] = sum;
    }
    Output[pixel] = float4(sum.rgb / max(sum.a, 1.0f), 1.0f);
}
