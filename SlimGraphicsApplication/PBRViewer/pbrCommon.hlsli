#ifndef PBR_COMMON_HLSLI
#define PBR_COMMON_HLSLI

// Shared by every PBR viewer shader: bindings common to all passes, packing, random numbers and sampling.
// Register layout (see pbrRenderer.cpp):
//   b0 FrameConstants, b1 pass constants
//   t0..t8 scene (vertices, indices, meshlets, meshlet vertices, meshlet triangles, meshes, instances, materials, TLAS),
//   t9 sky radiance (equirectangular), t10..t15 pass inputs
//   u0..u7 pass outputs
//   s0 anisotropic wrap, s3 trilinear wrap, s4 linear clamp, s5 point clamp
//   Material textures: bindless (ResourceDescriptorHeap)

#include "pbrShaderStructures.h"

static const float PI = 3.14159265358979f;
static const float TWO_PI = 6.28318530717959f;
static const float INV_PI = 0.31830988618379f;
static const float FLT_MAX = 3.402823466e+38f;

cbuffer FrameConstantBuffer : register(b0)
{
    FrameConstants frame;
};

StructuredBuffer<Vertex> Vertices : register(t0);
StructuredBuffer<uint> Indices : register(t1);
StructuredBuffer<Meshlet> Meshlets : register(t2);
StructuredBuffer<uint> MeshletVertices : register(t3);
StructuredBuffer<uint> MeshletTriangles : register(t4);
StructuredBuffer<Mesh> Meshes : register(t5);
StructuredBuffer<Instance> Instances : register(t6);
StructuredBuffer<Material> Materials : register(t7);
RaytracingAccelerationStructure SceneBVH : register(t8);
Texture2D<float4> SkyTexture : register(t9);

SamplerState AnisotropicSampler : register(s0);
SamplerState TrilinearSampler : register(s3);
SamplerState LinearClampSampler : register(s4);
SamplerState PointClampSampler : register(s5);

bool HasFlag(uint flag)
{
    return (frame.flags & flag) != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Packing

// Visibility buffer: x = instance + 1 (0 = nothing drawn), y = meshlet << 7 | triangle
uint2 PackVisibility(uint instance, uint meshlet, uint triangle_index)
{
    return uint2(instance + 1, (meshlet << 7) | triangle_index);
}

float2 OctWrap(float2 v)
{
    return (1.0f - abs(v.yx)) * select(v.xy >= 0.0f, 1.0f, -1.0f);
}

float2 EncodeOctahedral(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0f ? n.xy : OctWrap(n.xy);
    return n.xy * 0.5f + 0.5f;
}

float3 DecodeOctahedral(float2 f)
{
    f = f * 2.0f - 1.0f;
    float3 n = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0f, -t, t);
    return normalize(n);
}

// Denoiser guide: linear depth and the shading normal (octahedral, 12 bits per axis) with the roughness (8 bits)
uint2 PackGuide(float linear_depth, float3 normal, float roughness)
{
    float2 o = EncodeOctahedral(normal);
    uint2 q = (uint2)(saturate(o) * 4095.0f + 0.5f);
    return uint2(asuint(linear_depth), q.x | (q.y << 12) | ((uint)(saturate(roughness) * 255.0f + 0.5f) << 24));
}

void UnpackGuide(uint2 g, out float linear_depth, out float3 normal, out float roughness)
{
    linear_depth = asfloat(g.x);
    normal = DecodeOctahedral(float2(g.y & 0xFFF, (g.y >> 12) & 0xFFF) / 4095.0f);
    roughness = (g.y >> 24) / 255.0f;
}

float SRGBToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : pow((c + 0.055f) / 1.055f, 2.4f);
}

float3 SRGBToLinear(float3 c)
{
    return float3(SRGBToLinear(c.r), SRGBToLinear(c.g), SRGBToLinear(c.b));
}

float3 LinearToSRGB(float3 c)
{
    return select(c <= 0.0031308f, c * 12.92f, 1.055f * pow(max(c, 0.0f), 1.0f / 2.4f) - 0.055f);
}

float Luminance(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// ---------------------------------------------------------------------------------------------------------------------
// Random numbers

uint PCGHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

struct RNG
{
    uint state;

    float Next()
    {
        state = state * 747796405u + 2891336453u;
        uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        word = (word >> 22u) ^ word;
        return (word >> 8) * (1.0f / 16777216.0f);
    }

    float2 Next2()
    {
        return float2(Next(), Next());
    }
};

RNG CreateRNG(uint2 pixel, uint frame_index, uint stream)
{
    RNG rng;
    rng.state = PCGHash(pixel.x + PCGHash(pixel.y + PCGHash(frame_index + PCGHash(stream))));
    return rng;
}

// ---------------------------------------------------------------------------------------------------------------------
// Frames and sampling

void BuildBasis(float3 n, out float3 t, out float3 b)
{
    // Duff et al., "Building an Orthonormal Basis, Revisited"
    float s = n.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (s + n.z);
    float c = n.x * n.y * a;
    t = float3(1.0f + s * n.x * n.x * a, s * c, -s * n.x);
    b = float3(c, s + n.y * n.y * a, -n.y);
}

float3 ToWorld(float3 v, float3 n)
{
    float3 t, b;
    BuildBasis(n, t, b);
    return v.x * t + v.y * b + v.z * n;
}

float3 ToLocal(float3 v, float3 n)
{
    float3 t, b;
    BuildBasis(n, t, b);
    return float3(dot(v, t), dot(v, b), dot(v, n));
}

float3 SampleCosineHemisphere(float2 u)
{
    float r = sqrt(u.x);
    float phi = TWO_PI * u.y;
    return float3(r * cos(phi), r * sin(phi), sqrt(max(1.0f - u.x, 0.0f)));
}

// Uniform direction in a cone around +z with the given cos(half angle)
float3 SampleCone(float2 u, float cos_max)
{
    float cos_theta = lerp(1.0f, cos_max, u.x);
    float sin_theta = sqrt(max(1.0f - cos_theta * cos_theta, 0.0f));
    float phi = TWO_PI * u.y;
    return float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
}

// Equirectangular sky texture: u = azimuth, v = from up (0) to down (1)
float2 DirectionToEquirect(float3 d)
{
    float u = atan2(d.z, d.x) * (0.5f * INV_PI) + 0.5f;
    float v = acos(clamp(d.y, -1.0f, 1.0f)) * INV_PI;
    return float2(u, v);
}

float3 EquirectToDirection(float2 uv)
{
    float phi = (uv.x - 0.5f) * TWO_PI;
    float theta = uv.y * PI;
    return float3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
}

// Sky radiance without the sun disk (the sun is sampled as a light)
float3 SampleSky(float3 direction)
{
    // White furnace: a uniform unit sky (with white materials and no sun, every surface should reflect exactly that)
    if (frame.debug_view == PBR_DEBUG_WHITE_FURNACE)
    {
        return frame.sky_intensity;
    }
    return SkyTexture.SampleLevel(LinearClampSampler, DirectionToEquirect(direction), 0).rgb * frame.sky_intensity;
}

// Sun disk radiance along a direction (seen directly or by specular paths)
float3 SunDisk(float3 direction)
{
    return dot(direction, frame.sun_direction.xyz) >= frame.sun_direction.w ? frame.sun_disk_radiance.rgb : 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Camera

// View space direction (z = 1) through a screen position, including the projection's jitter. Camera relative: going
// through the inverse view projection matrix instead loses precision to the camera's distance from the origin.
float3 ViewRayDirection(float2 uv)
{
    float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    return float3((ndc.x - frame.projection._31) / frame.projection._11, (ndc.y - frame.projection._32) / frame.projection._22, 1.0f);
}

// View space to world space rotation (the view matrix's rotation is orthonormal: its inverse is its transpose)
float3 ViewToWorldDirection(float3 d)
{
    return mul((float3x3)frame.view, d);
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    // Reverse Z, infinite far plane: view z = near / depth
    float z = frame.camera_position.w / max(depth, 1e-20f);
    return frame.camera_position.xyz + ViewToWorldDirection(ViewRayDirection(uv) * z);
}

// Primary ray through a pixel (jittered like the rasterised frame)
void CameraRay(float2 pixel, out float3 origin, out float3 direction)
{
    origin = frame.camera_position.xyz;
    direction = normalize(ViewToWorldDirection(ViewRayDirection(pixel * frame.inverse_resolution)));
}

float LinearDepth(float depth)
{
    // Reverse Z, infinite far plane: depth = near / z
    return frame.camera_position.w / max(depth, 1e-8f);
}

#endif
