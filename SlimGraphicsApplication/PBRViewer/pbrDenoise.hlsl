// Spatiotemporal variance guided filtering (Schied et al. 2017, "SVGF") of the one sample per pixel ray traced signals:
//   temporal: reprojects last frame's history (rejecting disoccluded or mismatched surfaces) and blends in the new
//             sample; tracks luminance moments and the history length
//   variance: per pixel variance from the moments (or a spatial estimate while the history is short)
//   atrous:   edge aware wavelet filter (3x3 kernel, steps 1, 2, 4, ...) guided by depth, normals and luminance variance;
//             with pbr.denoise.filtered_history, the first iteration's result is next frame's history (otherwise the
//             temporal pass keeps the unfiltered running mean)
// Signals: diffuse, specular (roughness aware) and the sun shadow (PBR_SIGNAL_*)

#include "pbrCommon.hlsli"

ConstantBuffer<DenoiseConstants> pass : register(b1);

Texture2D<float4> Input : register(t10);            // temporal: raw signal; variance / a-trous: colour (+ variance)
Texture2D<float4> HistoryColour : register(t11);    // temporal: last frame's history
Texture2D<float4> Moments : register(t12);          // temporal: last frame's moments; variance: this frame's
Texture2D<float2> Motion : register(t13);
Texture2D<uint2> Guides : register(t14);            // PackGuide: linear depth, normal, roughness (this frame)
Texture2D<uint2> PreviousGuides : register(t15);

RWTexture2D<float4> Output : register(u0);
RWTexture2D<float4> OutputMoments : register(u1);   // temporal: moments; a-trous (write_history): history for next frame

float SignalLuminance(float3 c)
{
    return pass.signal == PBR_SIGNAL_SHADOW ? c.r : Luminance(c);
}

Guide LoadGuide(int2 p)
{
    return UnpackGuide(Guides[clamp(p, 0, (int2)frame.resolution - 1)]);
}

// Screen position of this pixel's surface in the last frame. Smooth reflections move with the reflected scene: they
// reproject the virtual image point, behind the surface along the view ray at the reflection's hit distance.
float2 PreviousUV(uint2 pixel, Guide current, float hit_distance)
{
    float2 uv = PixelCentreUV(pixel);
    float2 previous_uv = uv - Motion[pixel];
    if (pass.signal == PBR_SIGNAL_SPECULAR && hit_distance > 0.0f)
    {
        float3 direction = CameraRayDirection(uv);
        float3 forward = float3(frame.view._13, frame.view._23, frame.view._33);
        float3 virtual_point = frame.camera_position.xyz + direction * (current.depth / max(dot(direction, forward), 1e-4f) + hit_distance);
        float2 virtual_uv = ProjectToUV(float4(virtual_point, 1.0f), frame.previous_view_projection_no_jitter);
        if (virtual_uv.x >= 0.0f)
        {
            previous_uv = lerp(virtual_uv, previous_uv, smoothstep(0.1f, 0.4f, current.roughness));
        }
    }
    return previous_uv;
}

[numthreads(8, 8, 1)]
void temporal(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (!IsOnScreen(pixel))
    {
        return;
    }
    Guide current = LoadGuide(pixel);
    float4 sample_value = Input[pixel];
    if (current.depth <= 0.0f)
    {
        Output[pixel] = 0.0f;
        OutputMoments[pixel] = 0.0f;
        return;
    }

    // Bilinear reprojection, keeping only taps of the same surface
    float2 previous_pixel = PreviousUV(pixel, current, sample_value.a) * frame.resolution - 0.5f;
    int2 base = (int2)floor(previous_pixel);
    float2 f = previous_pixel - base;
    float4 history = 0.0f;
    float4 moments = 0.0f;
    float weight_sum = 0.0f;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        int2 tap = base + int2(i & 1, i >> 1);
        if (any(tap < 0) || any(tap >= (int2)frame.resolution))
        {
            continue;
        }
        Guide previous = UnpackGuide(PreviousGuides[tap]);
        bool roughness_matches = pass.signal != PBR_SIGNAL_SPECULAR || abs(previous.roughness - current.roughness) < 0.1f;
        if (SameSurface(current, previous) && roughness_matches)
        {
            float w = (i & 1 ? f.x : 1.0f - f.x) * (i >> 1 ? f.y : 1.0f - f.y);
            history += HistoryColour[tap] * w;
            moments += Moments[tap] * w;
            weight_sum += w;
        }
    }

    // Reflections move with the reflected scene, not the surface: shorter histories on smooth surfaces limit lag
    float max_length = pass.signal == PBR_SIGNAL_SPECULAR ? lerp(4.0f, (float)pass.max_history, saturate(current.roughness * 2.0f)) : (float)pass.max_history;
    float history_length = 1.0f;
    if (weight_sum > 1e-3f)
    {
        history /= weight_sum;
        moments /= weight_sum;
        history_length = min(moments.z + 1.0f, max_length);
    }
    float luminance = SignalLuminance(sample_value.rgb);
    float alpha = history_length > 1.0f ? max(1.0f / history_length, pass.colour_blend) : 1.0f;
    float alpha_moments = history_length > 1.0f ? max(1.0f / history_length, pass.moments_blend) : 1.0f;
    Output[pixel] = lerp(history, sample_value, alpha);
    OutputMoments[pixel] = float4(lerp(moments.xy, float2(luminance, luminance * luminance), alpha_moments), history_length, 0.0f);
}

[numthreads(8, 8, 1)]
void variance(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (!IsOnScreen(pixel))
    {
        return;
    }
    float4 colour = Input[pixel];
    float4 moments = Moments[pixel];
    Guide centre = LoadGuide(pixel);
    if (centre.depth <= 0.0f)
    {
        Output[pixel] = 0.0f;
        return;
    }
    float v = max(moments.y - moments.x * moments.x, 0.0f);
    if (moments.z < 4.0f)
    {
        // Short history: the variance of the neighbourhood (bilateral 7x7), boosted while the history builds up
        float2 m = 0.0f;
        float w_sum = 0.0f;
        for (int y = -3; y <= 3; y++)
        {
            for (int x = -3; x <= 3; x++)
            {
                int2 p = (int2)pixel + int2(x, y);
                Guide g = LoadGuide(p);
                if (g.depth > 0.0f)
                {
                    float w = pow(max(dot(g.normal, centre.normal), 0.0f), 64.0f) * exp(-abs(g.depth - centre.depth) / (0.05f * centre.depth + 1e-3f));
                    float l = SignalLuminance(Input[clamp(p, 0, (int2)frame.resolution - 1)].rgb);
                    m += float2(l, l * l) * w;
                    w_sum += w;
                }
            }
        }
        m /= max(w_sum, 1e-6f);
        v = max(m.y - m.x * m.x, 0.0f) * (4.0f / max(moments.z, 1.0f));
    }
    Output[pixel] = float4(colour.rgb, v);
}

// 3x3 a-trous kernel: [1 2 1] / 4 in each direction, relative to the centre's 1/4 (edges 1/2, corners 1/4)
float AtrousKernel(int2 offset)
{
    return (offset.x == 0 ? 1.0f : 0.5f) * (offset.y == 0 ? 1.0f : 0.5f);
}

[numthreads(8, 8, 1)]
void atrous(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (!IsOnScreen(pixel))
    {
        return;
    }
    float4 centre_value = Input[pixel];
    Guide centre = LoadGuide(pixel);
    if (centre.depth <= 0.0f)
    {
        Output[pixel] = 0.0f;
        if (pass.write_history != 0)
        {
            OutputMoments[pixel] = 0.0f;
        }
        return;
    }
    int step = (int)pass.step;
    bool specular = pass.signal == PBR_SIGNAL_SPECULAR;
    // Smooth reflections keep their detail: the kernel's reach shrinks with roughness
    float spatial = specular ? saturate(centre.roughness * centre.roughness * 8.0f) : 1.0f;
    float phi_normal = pass.phi_normal * (specular ? lerp(4.0f, 1.0f, saturate(centre.roughness * 2.0f)) : 1.0f);

    // Steps 1, 2, 4, ... cover the same area as a 5x5 kernel with one more iteration, at a third of the taps. Taps are
    // loaded once.
    float4 values[9];
    Guide guides[9];
    [unroll]
    for (int i = 0; i < 9; i++)
    {
        int2 p = clamp((int2)pixel + int2(i % 3 - 1, i / 3 - 1) * step, 0, (int2)frame.resolution - 1);
        values[i] = Input[p];
        guides[i] = UnpackGuide(Guides[p]);
    }

    float variance = centre_value.a;
    if (step == 1)
    {
        // The first iteration blurs the variance (stabilises the luminance edge stopping) and floors it with the luminance
        // spread of the same surface's neighbourhood: per pixel moments of sparse, bright samples (emissive light samples
        // that rarely hit) are mostly zero, which would make the filter reject exactly the samples that carry the light
        variance = 0.0f;
        float2 m = 0.0f;
        float m_weight = 0.0f;
        [unroll]
        for (int i = 0; i < 9; i++)
        {
            float kernel = AtrousKernel(int2(i % 3 - 1, i / 3 - 1)) * 0.25f;
            variance += values[i].a * kernel;
            float w = SameSurface(centre, guides[i]) ? kernel : 0.0f;
            float l = SignalLuminance(values[i].rgb);
            m += float2(l, l * l) * w;
            m_weight += w;
        }
        m /= max(m_weight, 1e-6f);
        variance = max(variance, max(m.y - m.x * m.x, 0.0f) * pass.spatial_variance);
    }
    float centre_luminance = SignalLuminance(centre_value.rgb);
    float luminance_scale = pass.phi_colour * sqrt(max(variance, 0.0f)) + 1e-4f;

    float3 sum = centre_value.rgb;
    float weight_sum = 1.0f;
    float variance_sum = centre_value.a;
    [unroll]
    for (int i = 0; i < 9; i++)
    {
        int2 offset = int2(i % 3 - 1, i / 3 - 1);
        int2 p = (int2)pixel + offset * step;
        Guide g = guides[i];
        if (i == 4 || any(p < 0) || any(p >= (int2)frame.resolution) || g.depth <= 0.0f)
        {
            continue;
        }
        float w_depth = exp(-abs(g.depth - centre.depth) / (pass.phi_depth * centre.depth * step + 1e-4f));
        float w_normal = pow(max(dot(g.normal, centre.normal), 0.0f), phi_normal);
        float w_luminance = exp(-abs(SignalLuminance(values[i].rgb) - centre_luminance) / luminance_scale);
        float w_roughness = specular ? exp(-abs(g.roughness - centre.roughness) * 10.0f) : 1.0f;
        float w = AtrousKernel(offset) * w_depth * w_normal * w_luminance * w_roughness * spatial;
        sum += values[i].rgb * w;
        variance_sum += values[i].a * w * w;
        weight_sum += w;
    }
    float4 result = float4(sum / weight_sum, variance_sum / (weight_sum * weight_sum));
    Output[pixel] = result;
    if (pass.write_history != 0)
    {
        OutputMoments[pixel] = float4(result.rgb, centre_value.a);
    }
}
