// Sky radiance: single scattering through a physically based atmosphere (Rayleigh, Mie and ozone over a spherical Earth),
// written to an equirectangular texture without the sun disk (the sun is a separate light). Lighting and the path tracer
// read the texture for rays that leave the scene.
//
// Pass constants: f0.rgb = solar illuminance at the top of the atmosphere, f0.w = Mie anisotropy g,
// f1 = (Mie density scale, ozone scale, Rayleigh scale, ground albedo), u0.xy = texture size

#include "pbrCommon.hlsli"

cbuffer SkyConstants : register(b1)
{
    PassConstants pass;
};

RWTexture2D<float4> SkyOutput : register(u0);
RWTexture2D<float4> MultipleScatteringOutput : register(u1);
Texture2D<float4> MultipleScattering : register(t10);       // (sun zenith cos, altitude) -> isotropic multiple scattering

#define MS_LUT_SIZE 32

static const float3 RAYLEIGH_SCATTERING = float3(PBR_RAYLEIGH_SCATTERING_R, PBR_RAYLEIGH_SCATTERING_G, PBR_RAYLEIGH_SCATTERING_B);
static const float3 OZONE_ABSORPTION = float3(PBR_OZONE_ABSORPTION_R, PBR_OZONE_ABSORPTION_G, PBR_OZONE_ABSORPTION_B);

// Distance along a ray (from inside the sphere) to a sphere of the given radius around the Earth's centre; < 0 if missed
float RaySphereExit(float3 origin, float3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float d = b * b - c;
    return d < 0.0f ? -1.0f : -b + sqrt(d);
}

float RaySphereEntry(float3 origin, float3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float d = b * b - c;
    if (d < 0.0f)
    {
        return -1.0f;
    }
    float t = -b - sqrt(d);
    return t;
}

// Extinction coefficients at an altitude
void MediumAt(float altitude, out float3 rayleigh, out float mie_scattering, out float3 extinction)
{
    float rayleigh_density = exp(-altitude / PBR_RAYLEIGH_SCALE_HEIGHT) * pass.f1.z;
    float mie_density = exp(-altitude / PBR_MIE_SCALE_HEIGHT) * pass.f1.x;
    float ozone_density = max(0.0f, 1.0f - abs(altitude - PBR_OZONE_CENTRE) / PBR_OZONE_WIDTH) * pass.f1.y;
    rayleigh = RAYLEIGH_SCATTERING * rayleigh_density;
    mie_scattering = PBR_MIE_SCATTERING * mie_density;
    extinction = rayleigh + PBR_MIE_EXTINCTION * mie_density + OZONE_ABSORPTION * ozone_density;
}

float3 TransmittanceToSun(float3 position, float3 sun)
{
    if (RaySphereEntry(position, sun, PBR_EARTH_RADIUS) > 0.0f)
    {
        return 0.0f; // Below the horizon
    }
    float length_to_top = RaySphereExit(position, sun, PBR_ATMOSPHERE_RADIUS);
    const uint steps = 12;
    float dt = length_to_top / steps;
    float3 optical_depth = 0.0f;
    for (uint i = 0; i < steps; i++)
    {
        float3 p = position + sun * ((i + 0.5f) * dt);
        float3 rayleigh, extinction;
        float mie;
        MediumAt(length(p) - PBR_EARTH_RADIUS, rayleigh, mie, extinction);
        optical_depth += extinction * dt;
    }
    return exp(-optical_depth);
}

float RayleighPhase(float cos_theta)
{
    return 3.0f / (16.0f * PI) * (1.0f + cos_theta * cos_theta);
}

float MiePhase(float cos_theta, float g)
{
    // Cornette-Shanks
    float g2 = g * g;
    return 3.0f / (8.0f * PI) * ((1.0f - g2) * (1.0f + cos_theta * cos_theta)) / ((2.0f + g2) * pow(max(1.0f + g2 - 2.0f * g * cos_theta, 1e-4f), 1.5f));
}

float2 MultipleScatteringUV(float cos_sun_zenith, float altitude)
{
    float u = saturate(cos_sun_zenith * 0.5f + 0.5f);
    float v = saturate(altitude / (PBR_ATMOSPHERE_RADIUS - PBR_EARTH_RADIUS));
    // Texel centres at the ends of the range
    return (float2(u, v) * (MS_LUT_SIZE - 1) + 0.5f) / MS_LUT_SIZE;
}

// Multiple scattering LUT (Hillaire 2020, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"): at an
// altitude and sun angle, the second order scattered light L2 and the transfer factor f_ms (light scattered back into the
// point from a unit isotropic source), integrated over the sphere; infinite orders sum to L2 / (1 - f_ms). Per unit solar
// illuminance (the sky pass scales it).
[numthreads(8, 8, 1)]
void multiple_scattering(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= MS_LUT_SIZE))
    {
        return;
    }
    float2 uv = dtid.xy / (float)(MS_LUT_SIZE - 1);
    float cos_sun = uv.x * 2.0f - 1.0f;
    float altitude = max(uv.y * (PBR_ATMOSPHERE_RADIUS - PBR_EARTH_RADIUS), 10.0f);
    float3 sun = float3(sqrt(saturate(1.0f - cos_sun * cos_sun)), cos_sun, 0.0f);
    float3 origin = float3(0.0f, PBR_EARTH_RADIUS + altitude, 0.0f);
    const uint directions = 64;
    const uint steps = 16;
    float3 second_order = 0.0f;
    float3 transfer = 0.0f;
    for (uint d = 0; d < directions; d++)
    {
        // Fibonacci sphere
        float z = 1.0f - (2.0f * d + 1.0f) / directions;
        float r = sqrt(saturate(1.0f - z * z));
        float phi = d * 2.39996323f;
        float3 direction = float3(r * cos(phi), z, r * sin(phi));
        float ground = RaySphereEntry(origin, direction, PBR_EARTH_RADIUS);
        bool hits_ground = ground > 0.0f;
        float length_through = hits_ground ? ground : RaySphereExit(origin, direction, PBR_ATMOSPHERE_RADIUS);
        float dt = length_through / steps;
        float3 transmittance = 1.0f;
        for (uint i = 0; i < steps; i++)
        {
            float3 p = origin + direction * ((i + 0.5f) * dt);
            float3 rayleigh, extinction;
            float mie;
            MediumAt(length(p) - PBR_EARTH_RADIUS, rayleigh, mie, extinction);
            float3 scattering = rayleigh + mie;
            float3 step_transmittance = exp(-extinction * dt);
            float3 integral = (1.0f - step_transmittance) / max(extinction, 1e-12f);
            second_order += transmittance * scattering * TransmittanceToSun(p, sun) * integral * (1.0f / (4.0f * PI));
            transfer += transmittance * scattering * integral;
            transmittance *= step_transmittance;
        }
        if (hits_ground)
        {
            float3 p = origin + direction * ground;
            float3 n = normalize(p);
            second_order += transmittance * TransmittanceToSun(p + n, sun) * saturate(dot(n, sun)) * pass.f1.w * INV_PI;
        }
    }
    // Mean over the sphere with the isotropic phase function
    second_order /= directions;
    transfer /= directions;
    MultipleScatteringOutput[dtid.xy] = float4(second_order / max(1.0f - transfer, 1e-3f), 1.0f);
}

[numthreads(8, 8, 1)]
void sky(uint3 dtid : SV_DispatchThreadID)
{
    uint2 size = pass.u0.xy;
    if (any(dtid.xy >= size))
    {
        return;
    }
    float3 direction = EquirectToDirection((dtid.xy + 0.5f) / (float2)size);
    float3 sun = frame.sun_direction.xyz;
    float3 origin = float3(0.0f, PBR_EARTH_RADIUS + PBR_VIEWER_ALTITUDE, 0.0f);

    // Segment through the atmosphere (to the ground, or out of the top)
    float ground = RaySphereEntry(origin, direction, PBR_EARTH_RADIUS);
    bool hits_ground = ground > 0.0f;
    float length_through = hits_ground ? ground : RaySphereExit(origin, direction, PBR_ATMOSPHERE_RADIUS);

    float cos_theta = dot(direction, sun);
    float phase_r = RayleighPhase(cos_theta);
    float phase_m = MiePhase(cos_theta, pass.f0.w);
    const uint steps = 32;
    float3 transmittance = 1.0f;
    float3 inscatter = 0.0f;
    // Quadratically distributed steps (density falls off exponentially with altitude)
    float previous_t = 0.0f;
    for (uint i = 0; i < steps; i++)
    {
        float f = (i + 1.0f) / steps;
        float t = length_through * f * f;
        float dt = t - previous_t;
        float3 p = origin + direction * (previous_t + 0.5f * dt);
        previous_t = t;
        float3 rayleigh, extinction;
        float mie;
        MediumAt(length(p) - PBR_EARTH_RADIUS, rayleigh, mie, extinction);
        float3 sun_transmittance = TransmittanceToSun(p, normalize(sun));
        float altitude = length(p) - PBR_EARTH_RADIUS;
        float3 up = p / length(p);
        float3 multiple = MultipleScattering.SampleLevel(LinearClampSampler, MultipleScatteringUV(dot(up, sun), altitude), 0).rgb;
        float3 scattering = rayleigh * phase_r + mie * phase_m;
        // Higher orders of scattering (isotropic, Hillaire 2020): brighter, whiter skies and horizons
        float3 multiple_inscatter = (rayleigh + mie) * multiple;
        // Analytic integration over the step (energy conserving for thick steps)
        float3 step_transmittance = exp(-extinction * dt);
        float3 integral = (1.0f - step_transmittance) / max(extinction, 1e-12f);
        inscatter += transmittance * (scattering * sun_transmittance + multiple_inscatter) * integral;
        transmittance *= step_transmittance;
    }
    float3 radiance = inscatter * pass.f0.rgb;

    if (hits_ground)
    {
        // Diffuse ground lit by the sun, seen through the atmosphere
        float3 p = origin + direction * ground;
        float3 n = normalize(p);
        float3 sun_at_ground = TransmittanceToSun(p + n, sun) * pass.f0.rgb * saturate(dot(n, sun));
        radiance += transmittance * sun_at_ground * pass.f1.w * INV_PI;
    }
    SkyOutput[dtid.xy] = float4(radiance, 1.0f);
}
