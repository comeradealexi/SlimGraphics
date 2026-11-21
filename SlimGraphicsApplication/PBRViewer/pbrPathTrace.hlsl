// Reference path tracer: progressive (one path per pixel per frame, averaged until the view or a setting changes), with the
// same materials, BRDF, sun and sky as the real time renderer.
//   - next event estimation of the sun (soft shadow rays over its disk) and of emissive triangles (power weighted) at every
//     vertex; emission found by BRDF sampling is combined with the latter by multiple importance sampling
//   - BRDF sampling: GGX visible normals for the specular lobe, cosine for diffuse, chosen by their estimated reflectance,
//     weighted by the whole BRDF over the mixture density
//   - the sky is found by BRDF sampling; Russian roulette after a few bounces
//
// Pass constants: u0 = (max bounces, Russian roulette start bounce, accumulated frames before this one, max frames)
//                 f0 = (pixel spread angle, unused...)

#include "pbrSurface.hlsli"

cbuffer PathTraceConstants : register(b1)
{
    PassConstants pass;
};

RWTexture2D<float4> Accumulation : register(u0);    // Sum of path radiance (rgb), path count (a)
RWTexture2D<float4> Output : register(u1);          // Average (HDR scene radiance)

// Clamps the exposed luminance of an indirect contribution (pbr.light.firefly_clamp, 0 = unbiased)
float3 ClampIndirect(float3 c)
{
    float l = Luminance(c) * frame.exposure;
    return frame.firefly_clamp > 0.0f && l > frame.firefly_clamp ? c * (frame.firefly_clamp / l) : c;
}

[numthreads(8, 8, 1)]
void path_trace(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= (uint2)frame.resolution))
    {
        return;
    }
    uint accumulated = pass.u0.z;
    float4 sum = accumulated > 0 ? Accumulation[pixel] : 0.0f;
    if (accumulated >= pass.u0.w)
    {
        Output[pixel] = float4(sum.rgb / max(sum.a, 1.0f), 1.0f);
        return;
    }

    RNG rng = CreateRNG(pixel, accumulated, 7);
    // Box filtered sub pixel position (anti aliasing converges with the samples)
    float3 origin, direction;
    CameraRay(pixel + rng.Next2(), origin, direction);

    float3 radiance = 0.0f;
    float3 throughput = 1.0f;
    float cone_width = 0.0f;
    float cone_spread = pass.f0.x;
    uint max_bounces = pass.u0.x;
    float previous_pdf = 0.0f;  // BRDF sampling density of the direction that reached this vertex (multiple importance sampling)
    bool sample_emissive = EmissiveSamplingEnabled();
    for (uint bounce = 0; bounce <= max_bounces; bounce++)
    {
        RayHit hit = TraceClosest(origin, direction, frame.max_ray_distance, cone_spread, NO_ALPHA_LIMIT);
        if (!hit.hit)
        {
            float3 sky = HasFlag(PBR_FLAG_SKY_LIGHTING) ? SampleSky(direction) : 0.0f;
            // The sun's disk is reached by next event estimation; only camera rays see it directly
            if (bounce == 0 && HasFlag(PBR_FLAG_SUN_DISK))
            {
                sky += SunDisk(direction);
            }
            radiance += throughput * (bounce > 0 ? ClampIndirect(sky) : sky);
            break;
        }
        cone_width += cone_spread * hit.t;
        SurfaceGeometry g;
        MaterialSample s;
        ResolveHit(hit, direction, cone_width, g, s);
        float3 v = -direction;

        // Emission found by BRDF sampling, weighted against emissive light sampling (power heuristic)
        float3 emission = s.emissive;
        if (bounce > 0 && sample_emissive && any(emission > 0.0f))
        {
            Material material = Materials[Meshes[Instances[hit.instance].mesh].material];
            float cos_light = max(abs(dot(g.geometric_normal, direction)), 1e-4f);
            float pdf_light = EmissiveAreaPdf(material) * hit.t * hit.t / cos_light;
            emission *= previous_pdf * previous_pdf / max(previous_pdf * previous_pdf + pdf_light * pdf_light, 1e-20f);
        }
        float3 vertex_light = emission + DirectSun(g, s, v, rng.Next2(), HasFlag(PBR_FLAG_SHADOWS), cone_spread, NO_ALPHA_LIMIT);

        // Lobe selection probabilities of the BRDF sampling below (also needed by the light sample's weight)
        float n_dot_v = max(dot(s.normal, v), 1e-4f);
        float3 f0 = SpecularF0(s);
        float3 specular_albedo = SpecularAlbedo(f0, s.roughness, n_dot_v);
        float3 diffuse_albedo = DiffuseColour(s) * (1.0f - specular_albedo);
        float specular_weight = Luminance(specular_albedo);
        float diffuse_weight = Luminance(diffuse_albedo);
        float p_specular = specular_weight / max(specular_weight + diffuse_weight, 1e-6f);

        // Emissive light sampling, weighted against BRDF sampling
        if (sample_emissive)
        {
            float3 light_diffuse, light_specular, irradiance, l;
            float pdf_light;
            if (EmissiveLight(g, s, v, rng, EmissiveCandidates(), NO_ALPHA_LIMIT, light_diffuse, light_specular, irradiance, pdf_light, l))
            {
                float pdf_brdf = BRDFPdf(s, v, l, p_specular);
                float w = pdf_light * pdf_light / max(pdf_light * pdf_light + pdf_brdf * pdf_brdf, 1e-20f);
                vertex_light += (light_diffuse + light_specular) * w;
            }
        }
        radiance += throughput * (bounce > 0 ? ClampIndirect(vertex_light) : vertex_light);
        if (bounce == max_bounces)
        {
            break;
        }

        // Continue the path: a direction from the specular or diffuse lobe (chosen by their estimated reflectance),
        // weighted by the whole BRDF over the mixture's density
        float alpha = s.roughness * s.roughness;
        float3 l;
        if (rng.Next() < p_specular)
        {
            float3 v_local = ToLocal(v, s.normal);
            float3 h = ToWorld(SampleGGXVNDF(float3(v_local.xy, n_dot_v), alpha, rng.Next2()), s.normal);
            l = reflect(-v, h);
            cone_spread += alpha * 0.5f;
        }
        else
        {
            l = ToWorld(SampleCosineHemisphere(rng.Next2()), s.normal);
            cone_spread += 0.3f;
        }
        if (dot(l, g.geometric_normal) <= 0.0f || dot(l, s.normal) <= 0.0f)
        {
            break; // Below the surface (shading normals can point there)
        }
        float3 brdf_diffuse, brdf_specular;
        EvaluateBRDF(s, v, l, brdf_diffuse, brdf_specular);
        float pdf = BRDFPdf(s, v, l, p_specular);
        if (pdf <= 0.0f)
        {
            break;
        }
        throughput *= (brdf_diffuse + brdf_specular) / pdf;
        previous_pdf = pdf;

        if (bounce >= pass.u0.y)
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
    if (any(isnan(radiance)) || any(isinf(radiance)))
    {
        radiance = 0.0f;
    }
    sum += float4(radiance, 1.0f);
    Accumulation[pixel] = sum;
    Output[pixel] = float4(sum.rgb / sum.a, 1.0f);
}
