// Sky radiance: single scattering through a physically based atmosphere (Rayleigh, Mie and ozone over a spherical Earth)
// plus Hillaire's multiple scattering approximation, written to an equirectangular texture without the sun disk (the sun is
// a separate light). Lighting and the path tracer read the texture for rays that leave the scene.

#include "pbrCommon.hlsli"

ConstantBuffer<SkyConstants> pass : register(b1);

RWTexture2D<float4> SkyOutput : register(u0);
RWTexture2D<float4> MultipleScatteringOutput : register(u1);
Texture2D<float4> MultipleScattering : register(t10);       // (sun zenith cos, altitude) -> isotropic multiple scattering

#define MS_LUT_SIZE 32

static const float3 RAYLEIGH_SCATTERING = float3(PBR_RAYLEIGH_SCATTERING_R, PBR_RAYLEIGH_SCATTERING_G, PBR_RAYLEIGH_SCATTERING_B);
static const float3 OZONE_ABSORPTION = float3(PBR_OZONE_ABSORPTION_R, PBR_OZONE_ABSORPTION_G, PBR_OZONE_ABSORPTION_B);

// Distances along a ray (from inside the outer sphere) to where it enters (x) and leaves (y) a sphere around the Earth's
// centre; negative when it misses
float2 RaySphere(float3 origin, float3 direction, float radius)
{
    float b = dot(origin, direction);
    float d = b * b - (dot(origin, origin) - radius * radius);
    return d < 0.0f ? -1.0f : -b + float2(-1.0f, 1.0f) * sqrt(d);
}

// The ray's path through the atmosphere: to the ground, or out of the top
float PathThroughAtmosphere(float3 origin, float3 direction, out bool hits_ground)
{
    float ground = RaySphere(origin, direction, PBR_EARTH_RADIUS).x;
    hits_ground = ground > 0.0f;
    return hits_ground ? ground : RaySphere(origin, direction, PBR_ATMOSPHERE_RADIUS).y;
}

struct Medium
{
    float3 rayleigh;        // Scattering coefficients
    float mie;
    float3 extinction;
};

Medium MediumAt(float3 position)
{
    float altitude = length(position) - PBR_EARTH_RADIUS;
    float mie_density = exp(-altitude / PBR_MIE_SCALE_HEIGHT) * pass.mie_density;
    float ozone_density = max(0.0f, 1.0f - abs(altitude - PBR_OZONE_CENTRE) / PBR_OZONE_WIDTH) * pass.ozone_density;
    Medium m;
    m.rayleigh = RAYLEIGH_SCATTERING * exp(-altitude / PBR_RAYLEIGH_SCALE_HEIGHT) * pass.rayleigh_density;
    m.mie = PBR_MIE_SCATTERING * mie_density;
    m.extinction = m.rayleigh + PBR_MIE_EXTINCTION * mie_density + OZONE_ABSORPTION * ozone_density;
    return m;
}

float3 TransmittanceToSun(float3 position, float3 sun)
{
    if (RaySphere(position, sun, PBR_EARTH_RADIUS).x > 0.0f)
    {
        return 0.0f; // Below the horizon
    }
    const uint steps = 12;
    float dt = RaySphere(position, sun, PBR_ATMOSPHERE_RADIUS).y / steps;
    float3 optical_depth = 0.0f;
    for (uint i = 0; i < steps; i++)
    {
        optical_depth += MediumAt(position + sun * ((i + 0.5f) * dt)).extinction * dt;
    }
    return exp(-optical_depth);
}

// Sunlight (per unit solar illuminance) reflected by the diffuse ground towards the sky
float3 GroundRadiance(float3 position, float3 sun)
{
    float3 n = normalize(position);
    return TransmittanceToSun(position + n, sun) * saturate(dot(n, sun)) * pass.ground_albedo * INV_PI;
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
    float2 uv = saturate(float2(cos_sun_zenith * 0.5f + 0.5f, altitude / (PBR_ATMOSPHERE_RADIUS - PBR_EARTH_RADIUS)));
    return (uv * (MS_LUT_SIZE - 1) + 0.5f) / MS_LUT_SIZE; // Texel centres at the ends of the range
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
    float3 sun = float3(sqrt(saturate(1.0f - cos_sun * cos_sun)), cos_sun, 0.0f);
    float3 origin = float3(0.0f, PBR_EARTH_RADIUS + max(uv.y * (PBR_ATMOSPHERE_RADIUS - PBR_EARTH_RADIUS), 10.0f), 0.0f);
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
        bool hits_ground;
        float dt = PathThroughAtmosphere(origin, direction, hits_ground) / steps;
        float3 transmittance = 1.0f;
        for (uint i = 0; i < steps; i++)
        {
            float3 p = origin + direction * ((i + 0.5f) * dt);
            Medium m = MediumAt(p);
            float3 step_transmittance = exp(-m.extinction * dt);
            float3 scattered = transmittance * (m.rayleigh + m.mie) * (1.0f - step_transmittance) / max(m.extinction, 1e-12f);
            second_order += scattered * TransmittanceToSun(p, sun) * (1.0f / (4.0f * PI));
            transfer += scattered;
            transmittance *= step_transmittance;
        }
        if (hits_ground)
        {
            second_order += transmittance * GroundRadiance(origin + direction * dt * steps, sun);
        }
    }
    // Mean over the sphere with the isotropic phase function
    MultipleScatteringOutput[dtid.xy] = float4(second_order / directions / max(1.0f - transfer / directions, 1e-3f), 1.0f);
}

[numthreads(8, 8, 1)]
void sky(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= pass.size))
    {
        return;
    }
    float3 direction = EquirectToDirection((dtid.xy + 0.5f) / (float2)pass.size);
    float3 sun = frame.sun_direction.xyz;
    float3 origin = float3(0.0f, PBR_EARTH_RADIUS + PBR_VIEWER_ALTITUDE, 0.0f);
    bool hits_ground;
    float length_through = PathThroughAtmosphere(origin, direction, hits_ground);

    float cos_theta = dot(direction, sun);
    float phase_rayleigh = RayleighPhase(cos_theta);
    float phase_mie = MiePhase(cos_theta, pass.mie_anisotropy);
    const uint steps = 32;
    float3 transmittance = 1.0f;
    float3 inscatter = 0.0f;
    float previous_t = 0.0f;
    for (uint i = 0; i < steps; i++)
    {
        // Quadratically distributed steps (density falls off exponentially with altitude)
        float f = (i + 1.0f) / steps;
        float t = length_through * f * f;
        float dt = t - previous_t;
        float3 p = origin + direction * (previous_t + 0.5f * dt);
        previous_t = t;
        Medium m = MediumAt(p);
        float3 up = normalize(p);
        // Higher orders of scattering (isotropic, Hillaire 2020): brighter, whiter skies and horizons
        float3 multiple = MultipleScattering.SampleLevel(LinearClampSampler, MultipleScatteringUV(dot(up, sun), length(p) - PBR_EARTH_RADIUS), 0).rgb;
        float3 source = (m.rayleigh * phase_rayleigh + m.mie * phase_mie) * TransmittanceToSun(p, sun) + (m.rayleigh + m.mie) * multiple;
        // Analytic integration over the step (energy conserving for thick steps)
        float3 step_transmittance = exp(-m.extinction * dt);
        inscatter += transmittance * source * (1.0f - step_transmittance) / max(m.extinction, 1e-12f);
        transmittance *= step_transmittance;
    }
    float3 radiance = inscatter;
    if (hits_ground)
    {
        // The diffuse ground lit by the sun, seen through the atmosphere
        radiance += transmittance * GroundRadiance(origin + direction * length_through, sun);
    }
    SkyOutput[dtid.xy] = float4(radiance * pass.solar_illuminance, 1.0f);
}
