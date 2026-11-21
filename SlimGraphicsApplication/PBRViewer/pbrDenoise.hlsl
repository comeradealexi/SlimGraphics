// Spatiotemporal variance guided filtering (Schied et al. 2017, "SVGF") of the one sample per pixel ray traced signals:
//   temporal: reprojects last frame's history (rejecting disoccluded or mismatched surfaces) and blends in the new
//             sample; tracks luminance moments and the history length
//   variance: per pixel variance from the moments (or a spatial estimate while the history is short)
//   atrous:   edge aware wavelet filter (3x3 kernel, steps 1, 2, 4, ...) guided by depth, normals and luminance variance;
//             with pbr.denoise.filtered_history, the first iteration's result is next frame's history (otherwise the
//             temporal pass keeps the unfiltered running mean)
// Signals (pass u0.x): 0 diffuse, 1 specular (roughness aware), 2 sun shadow
//
// Pass constants: u0 = (signal, a-trous step, write history, max history length)
//                 f0 = (colour blend minimum, moments blend minimum, phi colour, phi normal), f1 = (phi depth, spatial variance floor scale, unused...)

#include "pbrCommon.hlsli"

cbuffer DenoiseConstants : register(b1)
{
    PassConstants pass;
};

#define SIGNAL_DIFFUSE 0
#define SIGNAL_SPECULAR 1
#define SIGNAL_SHADOW 2

Texture2D<float4> Input : register(t10);            // temporal: raw signal; variance / a-trous: colour (+ variance)
Texture2D<float4> HistoryColour : register(t11);    // temporal: last frame's filtered history
Texture2D<float4> Moments : register(t12);          // temporal: last frame's moments; variance: this frame's
Texture2D<float2> Motion : register(t13);
Texture2D<uint2> Guide : register(t14);             // PackGuide: linear depth, normal, roughness (this frame)
Texture2D<uint2> PreviousGuide : register(t15);

RWTexture2D<float4> Output : register(u0);
RWTexture2D<float4> OutputMoments : register(u1);   // temporal: moments; a-trous step 1: history for next frame

float SignalLuminance(float3 c)
{
    return pass.u0.x == SIGNAL_SHADOW ? c.r : Luminance(c);
}

struct GuideSample
{
    float depth;
    float3 normal;
    float roughness;
};

GuideSample ReadGuide(uint2 g)
{
    GuideSample s;
    UnpackGuide(g, s.depth, s.normal, s.roughness);
    return s;
}

[numthreads(8, 8, 1)]
void temporal(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= (uint2)frame.resolution))
    {
        return;
    }
    GuideSample current = ReadGuide(Guide[pixel]);
    float4 sample_value = Input[pixel];
    if (current.depth <= 0.0f)
    {
        Output[pixel] = 0.0f;
        OutputMoments[pixel] = 0.0f;
        return;
    }

    // Bilinear reprojection, keeping only taps of the same surface
    float2 uv = (pixel + 0.5f) * frame.inverse_resolution;
    float2 previous_uv = uv - Motion[pixel];
    if (pass.u0.x == SIGNAL_SPECULAR && sample_value.a > 0.0f)
    {
        // Smooth reflections move with the reflected scene: reproject the virtual image point, which lies behind the
        // surface along the view ray at the reflection's hit distance (rough reflections keep the surface's motion)
        float3 direction = normalize(ReconstructWorldPosition(uv, 0.5f) - frame.camera_position.xyz);
        float3 forward = float3(frame.view._13, frame.view._23, frame.view._33);
        float3 position = frame.camera_position.xyz + direction * (current.depth / max(dot(direction, forward), 1e-4f));
        float3 virtual_point = position + direction * sample_value.a;
        float4 clip = mul(float4(virtual_point, 1.0f), frame.previous_view_projection_no_jitter);
        if (clip.w > 0.0f)
        {
            float2 virtual_uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
            previous_uv = lerp(virtual_uv, previous_uv, smoothstep(0.1f, 0.4f, current.roughness));
        }
    }
    float2 previous_pixel = previous_uv * frame.resolution - 0.5f;
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
        GuideSample previous = ReadGuide(PreviousGuide[tap]);
        bool depth_ok = abs(previous.depth - current.depth) < 0.05f * current.depth + 0.01f;
        bool normal_ok = dot(previous.normal, current.normal) > 0.9f;
        bool roughness_ok = pass.u0.x != SIGNAL_SPECULAR || abs(previous.roughness - current.roughness) < 0.1f;
        if (!(depth_ok && normal_ok && roughness_ok))
        {
            continue;
        }
        float w = (i & 1 ? f.x : 1.0f - f.x) * (i >> 1 ? f.y : 1.0f - f.y);
        history += HistoryColour[tap] * w;
        moments += Moments[tap] * w;
        weight_sum += w;
    }

    float max_length = (float)pass.u0.w;
    if (pass.u0.x == SIGNAL_SPECULAR)
    {
        // Reflections move with the reflected scene, not the surface: shorter histories on smooth surfaces limit lag
        max_length = lerp(4.0f, max_length, saturate(current.roughness * 2.0f));
    }
    float history_length = 0.0f;
    if (weight_sum > 1e-3f)
    {
        history /= weight_sum;
        moments /= weight_sum;
        history_length = min(moments.z + 1.0f, max_length);
    }
    else
    {
        history_length = 1.0f;
    }

    float luminance = SignalLuminance(sample_value.rgb);
    float alpha = history_length > 1.0f ? max(1.0f / history_length, pass.f0.x) : 1.0f;
    float alpha_moments = history_length > 1.0f ? max(1.0f / history_length, pass.f0.y) : 1.0f;
    float2 m = lerp(moments.xy, float2(luminance, luminance * luminance), alpha_moments);
    float3 colour = lerp(history.rgb, sample_value.rgb, alpha);
    Output[pixel] = float4(colour, lerp(history.a, sample_value.a, alpha));
    OutputMoments[pixel] = float4(m, history_length, 0.0f);
}

[numthreads(8, 8, 1)]
void variance(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= (uint2)frame.resolution))
    {
        return;
    }
    float4 colour = Input[pixel];
    float4 moments = Moments[pixel];
    GuideSample centre = ReadGuide(Guide[pixel]);
    if (centre.depth <= 0.0f)
    {
        Output[pixel] = 0.0f;
        return;
    }
    float v = max(moments.y - moments.x * moments.x, 0.0f);
    if (moments.z < 4.0f)
    {
        // Short history: estimate the variance from the neighbourhood (bilateral, 7x7)
        float2 m = 0.0f;
        float w_sum = 0.0f;
        for (int y = -3; y <= 3; y++)
        {
            for (int x = -3; x <= 3; x++)
            {
                int2 p = clamp((int2)pixel + int2(x, y), 0, (int2)frame.resolution - 1);
                GuideSample s = ReadGuide(Guide[p]);
                if (s.depth <= 0.0f)
                {
                    continue;
                }
                float w = pow(max(dot(s.normal, centre.normal), 0.0f), 64.0f) * exp(-abs(s.depth - centre.depth) / (0.05f * centre.depth + 1e-3f));
                float l = SignalLuminance(Input[p].rgb);
                m += float2(l, l * l) * w;
                w_sum += w;
            }
        }
        m /= max(w_sum, 1e-6f);
        // Boosted while the history builds up
        v = max(m.y - m.x * m.x, 0.0f) * (4.0f / max(moments.z, 1.0f));
    }
    Output[pixel] = float4(colour.rgb, v);
}

[numthreads(8, 8, 1)]
void atrous(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= (uint2)frame.resolution))
    {
        return;
    }
    float4 centre_value = Input[pixel];
    GuideSample centre = ReadGuide(Guide[pixel]);
    if (centre.depth <= 0.0f)
    {
        Output[pixel] = 0.0f;
        if (pass.u0.z != 0)
        {
            OutputMoments[pixel] = 0.0f;
        }
        return;
    }
    int step = (int)pass.u0.y;
    float phi_colour = pass.f0.z;
    float phi_normal = pass.f0.w;
    float phi_depth = pass.f1.x;

    // Smooth reflections keep their detail: the kernel's reach shrinks with roughness
    float spatial = 1.0f;
    if (pass.u0.x == SIGNAL_SPECULAR)
    {
        spatial = saturate(centre.roughness * centre.roughness * 8.0f);
        phi_normal *= lerp(4.0f, 1.0f, saturate(centre.roughness * 2.0f));
    }

    // 3x3 a-trous kernel ([1 2 1] / 4 in each direction; steps 1, 2, 4, ... cover the same area as a 5x5 kernel with one
    // more iteration, at a third of the taps). Taps are loaded once; the first iteration also blurs the variance with
    // them (stabilises the luminance edge stopping; later iterations already have filtered variance).
    float4 values[9];
    GuideSample guides[9];
    [unroll]
    for (int i = 0; i < 9; i++)
    {
        int2 offset = int2(i % 3 - 1, i / 3 - 1);
        int2 p = clamp((int2)pixel + offset * step, 0, (int2)frame.resolution - 1);
        values[i] = Input[p];
        guides[i] = ReadGuide(Guide[p]);
    }
    float variance = centre_value.a;
    if (step == 1)
    {
        variance = 0.0f;
        // Luminance spread of the neighbourhood's (same surface) values: per pixel moments of sparse, bright samples (emissive
        // light samples that rarely hit) are mostly zero, which would make the filter reject exactly the samples that carry
        // the light and darken the result. The neighbourhood sees what the pixel's own short window missed.
        float2 m = 0.0f;
        float m_weight = 0.0f;
        [unroll]
        for (int i = 0; i < 9; i++)
        {
            int2 offset = int2(i % 3 - 1, i / 3 - 1);
            float kernel = (offset.x == 0 ? 0.5f : 0.25f) * (offset.y == 0 ? 0.5f : 0.25f);
            variance += values[i].a * kernel;
            float w = guides[i].depth > 0.0f && abs(guides[i].depth - centre.depth) < 0.05f * centre.depth && dot(guides[i].normal, centre.normal) > 0.9f ? kernel : 0.0f;
            float l = SignalLuminance(values[i].rgb);
            m += float2(l, l * l) * w;
            m_weight += w;
        }
        m /= max(m_weight, 1e-6f);
        variance = max(variance, max(m.y - m.x * m.x, 0.0f) * pass.f1.y);
    }
    float centre_luminance = SignalLuminance(centre_value.rgb);
    float luminance_scale = phi_colour * sqrt(max(variance, 0.0f)) + 1e-4f;

    float4 sum = float4(centre_value.rgb, 0.0f);
    float weight_sum = 1.0f;
    float variance_sum = centre_value.a;
    [unroll]
    for (int i = 0; i < 9; i++)
    {
        int2 offset = int2(i % 3 - 1, i / 3 - 1);
        int2 p = (int2)pixel + offset * step;
        if (i == 4 || any(p < 0) || any(p >= (int2)frame.resolution) || guides[i].depth <= 0.0f)
        {
            continue;
        }
        GuideSample g = guides[i];
        float4 value = values[i];
        float w_depth = exp(-abs(g.depth - centre.depth) / (phi_depth * centre.depth * step + 1e-4f));
        float w_normal = pow(max(dot(g.normal, centre.normal), 0.0f), phi_normal);
        float w_luminance = exp(-abs(SignalLuminance(value.rgb) - centre_luminance) / luminance_scale);
        float w_roughness = pass.u0.x == SIGNAL_SPECULAR ? exp(-abs(g.roughness - centre.roughness) * 10.0f) : 1.0f;
        // Kernel weight relative to the centre's 1/4: edges 1/8, corners 1/16
        float kernel = (offset.x == 0 || offset.y == 0) ? 0.5f : 0.25f;
        float w = kernel * w_depth * w_normal * w_luminance * w_roughness * spatial;
        sum.rgb += value.rgb * w;
        variance_sum += value.a * w * w;
        weight_sum += w;
    }
    float4 result = float4(sum.rgb / weight_sum, variance_sum / (weight_sum * weight_sum));
    Output[pixel] = result;
    if (pass.u0.z != 0)
    {
        OutputMoments[pixel] = float4(result.rgb, centre_value.a);
    }
}
