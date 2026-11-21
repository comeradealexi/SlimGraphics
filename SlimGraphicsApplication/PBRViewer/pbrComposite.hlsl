// Combines the G-buffer with the denoised lighting into HDR scene radiance, and draws the debug views.
// Render targets are read through bindless descriptors (pass.u0..u3 hold their indices).

#include "pbrSurface.hlsli"

cbuffer CompositeConstants : register(b1)
{
    PassConstants pass;
};

RWTexture2D<float4> Output : register(u0);

Texture2D<float4> Target(uint index)
{
    return ResourceDescriptorHeap[index];
}

float3 Heatmap(float t)
{
    t = saturate(t);
    return saturate(float3(1.5f - abs(4.0f * t - 3.0f), 1.5f - abs(4.0f * t - 2.0f), 1.5f - abs(4.0f * t - 1.0f)));
}

float3 HashColour(uint v)
{
    uint h = PCGHash(v);
    return float3(h & 0xFF, (h >> 8) & 0xFF, (h >> 16) & 0xFF) / 255.0f * 0.8f + 0.2f;
}

[numthreads(8, 8, 1)]
void composite(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= (uint2)frame.resolution))
    {
        return;
    }
    float4 albedo_metallic = Target(pass.u0.x)[pixel];
    float4 normal_roughness = Target(pass.u0.y)[pixel];
    float4 geometry = Target(pass.u0.z)[pixel];
    float3 emissive = Target(pass.u0.w)[pixel].rgb;
    float depth = Target(pass.u1.x)[pixel].r;
    float shadow = Target(pass.u1.y)[pixel].r;
    // Lighting is stored pre-exposed (see pbrLighting.hlsl)
    float inverse_exposure = 1.0f / max(frame.exposure, 1e-20f);
    float3 diffuse_light = Target(pass.u1.z)[pixel].rgb * inverse_exposure;
    float3 specular_light = Target(pass.u1.w)[pixel].rgb * inverse_exposure;

    float2 uv = (pixel + 0.5f) * frame.inverse_resolution;
    float3 position = ReconstructWorldPosition(uv, max(depth, 1e-7f));
    float3 view_direction = normalize(position - frame.camera_position.xyz);

    // Debug views of data (shown without exposure or tone mapping, see pbrPost.hlsl)
    uint view = frame.debug_view;
    if (view != PBR_DEBUG_NONE && view != PBR_DEBUG_DIRECT && view != PBR_DEBUG_INDIRECT && view != PBR_DEBUG_DIFFUSE && view != PBR_DEBUG_SPECULAR &&
        view != PBR_DEBUG_DIFFUSE_RAW && view != PBR_DEBUG_SPECULAR_RAW && view != PBR_DEBUG_EMISSIVE && view != PBR_DEBUG_WHITE_FURNACE && view != PBR_DEBUG_RADIANCE_CACHE)
    {
        float3 c = 0.0f;
        switch (view)
        {
        case PBR_DEBUG_ALBEDO: c = albedo_metallic.rgb; break;
        case PBR_DEBUG_NORMALS: c = depth > 0.0f ? normal_roughness.xyz * 0.5f + 0.5f : 0.0f; break;
        case PBR_DEBUG_GEOMETRIC_NORMALS: c = depth > 0.0f ? DecodeOctahedral(geometry.xy) * 0.5f + 0.5f : 0.0f; break;
        case PBR_DEBUG_ROUGHNESS: c = normal_roughness.w; break;
        case PBR_DEBUG_METALLIC: c = albedo_metallic.a; break;
        case PBR_DEBUG_AO: c = geometry.z; break;
        case PBR_DEBUG_MOTION: c = float3(abs(Target(pass.u2.w)[pixel].xy) * 50.0f, 0.0f); break;
        case PBR_DEBUG_DEPTH: c = depth > 0.0f ? frac(log2(LinearDepth(depth)) * 0.5f) : 0.0f; break;
        case PBR_DEBUG_INSTANCES:
        case PBR_DEBUG_MESHLETS:
        case PBR_DEBUG_TRIANGLES:
        {
            Texture2D<uint2> visibility = ResourceDescriptorHeap[pass.u3.z];
            uint2 vis = visibility[pixel];
            uint id = view == PBR_DEBUG_INSTANCES ? vis.x : (view == PBR_DEBUG_MESHLETS ? vis.y >> 7 : vis.y);
            c = vis.x != 0 ? HashColour(id) : 0.0f;
            break;
        }
        case PBR_DEBUG_SHADOW_RAW: c = Target(pass.u2.x)[pixel].r; break;
        case PBR_DEBUG_SHADOW: c = shadow; break;
        case PBR_DEBUG_HISTORY_LENGTH: c = Heatmap(Target(pass.u3.x)[pixel].z / 32.0f); break;
        case PBR_DEBUG_VARIANCE: c = Heatmap(sqrt(Target(pass.u1.z)[pixel].a) * frame.exposure * 4.0f); break;
        case PBR_DEBUG_HZB:
        {
            float level = floor(frac(frame.frame_index / 120.0f) * frame.hzb_mip_count);
            Texture2D<float> hzb = ResourceDescriptorHeap[pass.u3.y];
            float d = hzb.SampleLevel(PointClampSampler, uv, level);
            c = d > 0.0f ? frac(log2(LinearDepth(d)) * 0.5f) : 0.0f;
            break;
        }
        }
        Output[pixel] = float4(c, 1.0f);
        return;
    }

    float3 radiance;
    if (depth <= 0.0f)
    {
        // Sky (with the sun's disk)
        radiance = HasFlag(PBR_FLAG_SKY_LIGHTING) ? SampleSky(view_direction) : 0.0f;
        radiance += HasFlag(PBR_FLAG_SUN_DISK) ? SunDisk(view_direction) : 0.0f;
        if (view == PBR_DEBUG_DIRECT || view == PBR_DEBUG_INDIRECT || view == PBR_DEBUG_DIFFUSE || view == PBR_DEBUG_SPECULAR || view == PBR_DEBUG_EMISSIVE ||
            view == PBR_DEBUG_DIFFUSE_RAW || view == PBR_DEBUG_SPECULAR_RAW || view == PBR_DEBUG_RADIANCE_CACHE)
        {
            radiance = 0.0f;
        }
    }
    else
    {
        MaterialSample s;
        s.albedo = albedo_metallic.rgb;
        s.metallic = albedo_metallic.a;
        s.normal = normalize(normal_roughness.xyz);
        s.roughness = normal_roughness.w;
        s.emissive = emissive;
        s.ao = geometry.z;
        s.opacity = 1.0f;
        float3 v = -view_direction;
        float n_dot_v = max(dot(s.normal, v), 1e-4f);
        float3 f0 = SpecularF0(s);
        float3 specular_albedo = SpecularAlbedo(f0, s.roughness, n_dot_v);

        float3 diffuse_brdf, specular_brdf;
        EvaluateBRDF(s, v, frame.sun_direction.xyz, diffuse_brdf, specular_brdf);
        float3 direct = (diffuse_brdf + specular_brdf) * frame.sun_illuminance.rgb * shadow;
        float3 indirect_diffuse = DiffuseColour(s) * (1.0f - specular_albedo) * diffuse_light * s.ao;
        float3 indirect_specular = specular_albedo * specular_light;

        switch (view)
        {
        case PBR_DEBUG_DIRECT: radiance = direct; break;
        case PBR_DEBUG_INDIRECT: radiance = indirect_diffuse + indirect_specular; break;
        case PBR_DEBUG_DIFFUSE: radiance = diffuse_light; break;
        case PBR_DEBUG_SPECULAR: radiance = specular_light; break;
        case PBR_DEBUG_DIFFUSE_RAW: radiance = Target(pass.u2.y)[pixel].rgb * inverse_exposure; break;
        case PBR_DEBUG_SPECULAR_RAW: radiance = Target(pass.u2.z)[pixel].rgb * inverse_exposure; break;
        case PBR_DEBUG_EMISSIVE: radiance = emissive; break;
        default: radiance = emissive + direct + indirect_diffuse + indirect_specular; break;
        }
    }
    Output[pixel] = float4(radiance, 1.0f);
}
