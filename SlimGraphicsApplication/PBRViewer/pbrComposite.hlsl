// Combines the G-buffer with the denoised lighting into HDR scene radiance, and draws the debug views. Render targets are
// read through their bindless indices.

#include "pbrSurface.hlsli"

ConstantBuffer<CompositeConstants> pass : register(b1);

RWTexture2D<float4> Output : register(u0);

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

float3 DepthBands(float depth)
{
    return depth > 0.0f ? frac(log2(LinearDepth(depth)) * 0.5f) : 0.0f;
}

// Debug views of data (shown without exposure or tone mapping, see pbrPost.hlsl)
float3 DataView(uint2 pixel, float2 uv, float depth)
{
    float4 albedo_metallic = BindlessTarget(pass.albedo_metallic)[pixel];
    float4 normal_roughness = BindlessTarget(pass.normal_roughness)[pixel];
    float4 geometry = BindlessTarget(pass.geometry)[pixel];
    bool surface = depth > 0.0f;
    switch (frame.debug_view)
    {
    case PBR_DEBUG_ALBEDO: return albedo_metallic.rgb;
    case PBR_DEBUG_NORMALS: return surface ? normal_roughness.xyz * 0.5f + 0.5f : 0.0f;
    case PBR_DEBUG_GEOMETRIC_NORMALS: return surface ? DecodeOctahedral(geometry.xy) * 0.5f + 0.5f : 0.0f;
    case PBR_DEBUG_ROUGHNESS: return normal_roughness.w;
    case PBR_DEBUG_METALLIC: return albedo_metallic.a;
    case PBR_DEBUG_AO: return geometry.z;
    case PBR_DEBUG_MOTION: return float3(abs(BindlessTarget(pass.motion)[pixel].xy) * 50.0f, 0.0f);
    case PBR_DEBUG_DEPTH: return DepthBands(depth);
    case PBR_DEBUG_INSTANCES:
    case PBR_DEBUG_MESHLETS:
    case PBR_DEBUG_TRIANGLES:
    {
        Texture2D<uint2> visibility = ResourceDescriptorHeap[pass.visibility];
        uint2 vis = visibility[pixel];
        uint id = frame.debug_view == PBR_DEBUG_INSTANCES ? vis.x : (frame.debug_view == PBR_DEBUG_MESHLETS ? vis.y >> 7 : vis.y);
        return vis.x != 0 ? HashColour(id) : 0.0f;
    }
    case PBR_DEBUG_SHADOW_RAW: return BindlessTarget(pass.shadow_raw)[pixel].r;
    case PBR_DEBUG_SHADOW: return BindlessTarget(pass.shadow)[pixel].r;
    case PBR_DEBUG_HISTORY_LENGTH: return Heatmap(BindlessTarget(pass.diffuse_moments)[pixel].z / 32.0f);
    case PBR_DEBUG_VARIANCE: return Heatmap(sqrt(BindlessTarget(pass.diffuse)[pixel].a) * frame.exposure * 4.0f);
    case PBR_DEBUG_HZB:
    {
        // Cycles through the levels
        Texture2D<float> hzb = ResourceDescriptorHeap[pass.hzb];
        return DepthBands(hzb.SampleLevel(PointClampSampler, uv, floor(frac(frame.frame_index / 120.0f) * frame.hzb_mip_count)));
    }
    default: return 0.0f;
    }
}

[numthreads(8, 8, 1)]
void composite(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (!IsOnScreen(pixel))
    {
        return;
    }
    float2 uv = PixelCentreUV(pixel);
    float depth = BindlessTarget(pass.depth)[pixel].r;
    uint view = frame.debug_view;
    if (view >= PBR_DEBUG_FIRST_DATA_VIEW)
    {
        Output[pixel] = float4(DataView(pixel, uv, depth), 1.0f);
        return;
    }

    float3 view_direction = CameraRayDirection(uv);
    float3 radiance = 0.0f;
    if (depth <= 0.0f)
    {
        // The sky (with the sun's disk), in the views of the whole image
        radiance = view <= PBR_DEBUG_WHITE_FURNACE ? MissRadiance(view_direction, true) : 0.0f;
    }
    else
    {
        // Lighting is stored pre-exposed (see pbrLighting.hlsl)
        float inverse_exposure = 1.0f / max(frame.exposure, 1e-20f);
        float3 diffuse_light = BindlessTarget(pass.diffuse)[pixel].rgb * inverse_exposure;
        float3 specular_light = BindlessTarget(pass.specular)[pixel].rgb * inverse_exposure;
        MaterialSample s = GBufferMaterial(BindlessTarget(pass.albedo_metallic)[pixel], BindlessTarget(pass.normal_roughness)[pixel],
            BindlessTarget(pass.geometry)[pixel].z, BindlessTarget(pass.emissive)[pixel].rgb);
        float3 v = -view_direction;
        float3 specular_albedo = SpecularAlbedo(s, v);
        float3 direct = BRDF(s, v, frame.sun_direction.xyz) * frame.sun_illuminance.rgb * BindlessTarget(pass.shadow)[pixel].r;
        float3 indirect = DiffuseColour(s) * (1.0f - specular_albedo) * diffuse_light * s.ao + specular_albedo * specular_light;
        switch (view)
        {
        case PBR_DEBUG_DIRECT: radiance = direct; break;
        case PBR_DEBUG_INDIRECT: radiance = indirect; break;
        case PBR_DEBUG_DIFFUSE:
        case PBR_DEBUG_RADIANCE_CACHE: radiance = diffuse_light; break;
        case PBR_DEBUG_SPECULAR: radiance = specular_light; break;
        case PBR_DEBUG_DIFFUSE_RAW: radiance = BindlessTarget(pass.diffuse_raw)[pixel].rgb * inverse_exposure; break;
        case PBR_DEBUG_SPECULAR_RAW: radiance = BindlessTarget(pass.specular_raw)[pixel].rgb * inverse_exposure; break;
        case PBR_DEBUG_EMISSIVE: radiance = s.emissive; break;
        default: radiance = s.emissive + direct + indirect; break;
        }
    }
    Output[pixel] = float4(radiance, 1.0f);
}
