// Temporal anti-aliasing, the exposure histogram and tone mapping.
// Render targets are read through bindless descriptors (pass.u0 holds their indices).

#include "pbrCommon.hlsli"

cbuffer PostConstants : register(b1)
{
    PassConstants pass;
};

Texture2D<float4> Target(uint index)
{
    return ResourceDescriptorHeap[index];
}

// Debug views of data skip exposure, tone mapping and temporal filtering
bool IsDataView(uint view)
{
    return view != PBR_DEBUG_NONE && view != PBR_DEBUG_DIRECT && view != PBR_DEBUG_INDIRECT && view != PBR_DEBUG_DIFFUSE && view != PBR_DEBUG_SPECULAR &&
        view != PBR_DEBUG_DIFFUSE_RAW && view != PBR_DEBUG_SPECULAR_RAW && view != PBR_DEBUG_EMISSIVE && view != PBR_DEBUG_WHITE_FURNACE && view != PBR_DEBUG_RADIANCE_CACHE;
}

// ---------------------------------------------------------------------------------------------------------------------
// TAA: pass.u0 = (current colour, history, motion, depth), pass.u1.x = history valid, f0.x = blend, f0.y = clip gamma

RWTexture2D<float4> TAAOutput : register(u0);

float3 RGBToYCoCg(float3 c)
{
    return float3(0.25f * c.r + 0.5f * c.g + 0.25f * c.b, 0.5f * c.r - 0.5f * c.b, -0.25f * c.r + 0.5f * c.g - 0.25f * c.b);
}

float3 YCoCgToRGB(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Catmull-Rom history sample from 5 bilinear taps (sharper than bilinear, less blur over time)
float3 SampleHistoryCatmullRom(Texture2D<float4> history, float2 uv)
{
    float2 position = uv * frame.resolution;
    float2 centre = floor(position - 0.5f) + 0.5f;
    float2 f = position - centre;
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);
    float2 w12 = w1 + w2;
    float2 tc0 = (centre - 1.0f) * frame.inverse_resolution;
    float2 tc3 = (centre + 2.0f) * frame.inverse_resolution;
    float2 tc12 = (centre + w2 / w12) * frame.inverse_resolution;
    float3 result = history.SampleLevel(LinearClampSampler, float2(tc12.x, tc0.y), 0).rgb * (w12.x * w0.y);
    result += history.SampleLevel(LinearClampSampler, float2(tc0.x, tc12.y), 0).rgb * (w0.x * w12.y);
    result += history.SampleLevel(LinearClampSampler, float2(tc12.x, tc12.y), 0).rgb * (w12.x * w12.y);
    result += history.SampleLevel(LinearClampSampler, float2(tc3.x, tc12.y), 0).rgb * (w3.x * w12.y);
    result += history.SampleLevel(LinearClampSampler, float2(tc12.x, tc3.y), 0).rgb * (w12.x * w3.y);
    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(result / weight, 0.0f);
}

[numthreads(8, 8, 1)]
void taa(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= (uint2)frame.resolution))
    {
        return;
    }
    Texture2D<float4> current = Target(pass.u0.x);
    float3 colour = current[pixel].rgb;
    if (pass.u1.x == 0 || IsDataView(frame.debug_view))
    {
        TAAOutput[pixel] = float4(colour, 1.0f);
        return;
    }

    // Motion of the nearest surface in the neighbourhood (keeps edges of moving objects clean)
    Texture2D<float> depth = ResourceDescriptorHeap[pass.u0.w];
    int2 nearest = (int2)pixel;
    float nearest_depth = 0.0f;
    // Neighbourhood statistics in YCoCg, with tone mapped weights (fireflies do not stretch the clipping box)
    float3 m1 = 0.0f, m2 = 0.0f;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            int2 p = clamp((int2)pixel + int2(x, y), 0, (int2)frame.resolution - 1);
            float3 c = RGBToYCoCg(current[p].rgb * frame.exposure);
            c /= 1.0f + c.x;
            m1 += c;
            m2 += c * c;
            float d = depth[p];
            if (d > nearest_depth)
            {
                nearest_depth = d;
                nearest = p;
            }
        }
    }
    Texture2D<float2> motion_texture = ResourceDescriptorHeap[pass.u0.z];
    float2 motion = motion_texture[nearest];
    float2 previous_uv = (pixel + 0.5f) * frame.inverse_resolution - motion;
    if (any(previous_uv < 0.0f) || any(previous_uv > 1.0f))
    {
        TAAOutput[pixel] = float4(colour, 1.0f);
        return;
    }
    float3 history = SampleHistoryCatmullRom(Target(pass.u0.y), previous_uv);

    float3 mean = m1 / 9.0f;
    float3 sigma = sqrt(max(m2 / 9.0f - mean * mean, 0.0f));
    float3 box_min = mean - pass.f0.y * sigma;
    float3 box_max = mean + pass.f0.y * sigma;
    float3 history_ycocg = RGBToYCoCg(history * frame.exposure);
    history_ycocg /= 1.0f + history_ycocg.x;
    // Clip towards the box centre (variance clipping)
    float3 centre = 0.5f * (box_max + box_min);
    float3 extent = 0.5f * (box_max - box_min) + 1e-5f;
    float3 offset = history_ycocg - centre;
    float3 units = abs(offset / extent);
    float max_unit = max(units.x, max(units.y, units.z));
    if (max_unit > 1.0f)
    {
        history_ycocg = centre + offset / max_unit;
    }
    history_ycocg /= max(1.0f - history_ycocg.x, 1e-4f);
    history = max(YCoCgToRGB(history_ycocg) / frame.exposure, 0.0f);

    // Luminance weighted blend (anti flicker)
    float3 colour_mapped = colour * frame.exposure;
    float w_current = pass.f0.x / (1.0f + Luminance(colour_mapped));
    float w_history = (1.0f - pass.f0.x) / (1.0f + Luminance(history * frame.exposure));
    float3 result = (colour * w_current + history * w_history) / (w_current + w_history);
    TAAOutput[pixel] = float4(result, 1.0f);
}

// ---------------------------------------------------------------------------------------------------------------------
// Exposure histogram: pass.u0.x = colour, f0 = (min log2 luminance, log2 luminance range); 256 bins, read back by the CPU

RWStructuredBuffer<uint> Histogram : register(u1);
groupshared uint s_histogram[256];

[numthreads(16, 16, 1)]
void histogram(uint3 dtid : SV_DispatchThreadID, uint gindex : SV_GroupIndex)
{
    s_histogram[gindex] = 0;
    GroupMemoryBarrierWithGroupSync();
    if (all(dtid.xy < (uint2)frame.resolution))
    {
        float l = Luminance(Target(pass.u0.x)[dtid.xy].rgb);
        if (l > 1e-8f)
        {
            uint bin = (uint)clamp((log2(l) - pass.f0.x) / pass.f0.y * 254.0f + 1.0f, 1.0f, 255.0f);
            InterlockedAdd(s_histogram[bin], 1);
        }
        else
        {
            InterlockedAdd(s_histogram[0], 1);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    InterlockedAdd(Histogram[gindex], s_histogram[gindex]);
}

// ---------------------------------------------------------------------------------------------------------------------
// Tone mapping: pass.u0.x = colour, u1.x = operator, f0.x = dither strength

RWTexture2D<float4> DisplayOutput : register(u2);

float3 ACESFitted(float3 c)
{
    // Stephen Hill's fit of the ACES RRT + ODT (sRGB primaries)
    const float3x3 input_matrix = float3x3(0.59719f, 0.35458f, 0.04823f, 0.07600f, 0.90834f, 0.01566f, 0.02840f, 0.13383f, 0.83777f);
    const float3x3 output_matrix = float3x3(1.60475f, -0.53108f, -0.07367f, -0.10208f, 1.10813f, -0.00605f, -0.00327f, -0.07276f, 1.07602f);
    c = mul(input_matrix, c);
    float3 a = c * (c + 0.0245786f) - 0.000090537f;
    float3 b = c * (0.983729f * c + 0.4329510f) + 0.238081f;
    return saturate(mul(output_matrix, a / b));
}

float3 AgXContrastApprox(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
}

float3 AgX(float3 c)
{
    // Minimal AgX (Troy Sobotka's base look, polynomial fit by Benjamin Wrensch)
    const float3x3 inset = float3x3(0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f, 0.0784335999999992f, 0.878468636469772f, 0.0784336f, 0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f);
    const float3x3 outset = float3x3(1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f, -0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f, -0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f);
    const float min_ev = -12.47393f;
    const float max_ev = 4.026069f;
    c = mul(c, inset);
    c = clamp(log2(max(c, 1e-10f)), min_ev, max_ev);
    c = (c - min_ev) / (max_ev - min_ev);
    c = AgXContrastApprox(c);
    c = mul(c, outset);
    return saturate(SRGBToLinear(saturate(c)));
}

float3 KhronosPBRNeutral(float3 c)
{
    const float start_compression = 0.8f - 0.04f;
    const float desaturation = 0.15f;
    float x = min(c.r, min(c.g, c.b));
    float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    c -= offset;
    float peak = max(c.r, max(c.g, c.b));
    if (peak < start_compression)
    {
        return c;
    }
    float d = 1.0f - start_compression;
    float new_peak = 1.0f - d * d / (peak + d - start_compression);
    c *= new_peak / peak;
    float g = 1.0f - 1.0f / (desaturation * (peak - new_peak) + 1.0f);
    return lerp(c, new_peak, g);
}

[numthreads(8, 8, 1)]
void tonemap(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= (uint2)frame.resolution))
    {
        return;
    }
    float3 c = Target(pass.u0.x)[pixel].rgb;
    if (IsDataView(frame.debug_view))
    {
        DisplayOutput[pixel] = float4(LinearToSRGB(saturate(c)), 1.0f);
        return;
    }
    c *= frame.exposure;
    switch (pass.u1.x)
    {
    case 0: c = saturate(c); break;
    case 1: c = c / (1.0f + Luminance(c)); break;
    case 2: c = ACESFitted(c); break;
    case 3: c = AgX(c); break;
    default: c = saturate(KhronosPBRNeutral(c)); break;
    }
    float3 display = LinearToSRGB(c);
    // Triangular dither of one 8 bit step (no banding in skies and soft gradients)
    uint h = PCGHash(pixel.x + PCGHash(pixel.y + PCGHash(frame.frame_index)));
    float2 u = float2(h & 0xFFFF, h >> 16) / 65535.0f;
    display += (u.x + u.y - 1.0f) * pass.f0.x / 255.0f;
    DisplayOutput[pixel] = float4(saturate(display), 1.0f);
}
