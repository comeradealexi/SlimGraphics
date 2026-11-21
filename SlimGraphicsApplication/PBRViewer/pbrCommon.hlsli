#ifndef PBR_COMMON_HLSLI
#define PBR_COMMON_HLSLI

// Shared by every PBR viewer shader: bindings common to all passes, packing, random numbers, sampling and the camera.
// Register layout (see Renderer::bind):
//   b0 FrameConstants, b1 the pass's constants
//   t0..t8 scene (vertices, indices, meshlets, meshlet vertices, meshlet triangles, meshes, instances, materials, TLAS),
//   t9 sky radiance (equirectangular), t10..t15 pass inputs
//   u0..u7 pass outputs
//   s0 anisotropic wrap, s3 trilinear wrap, s4 linear clamp, s5 point clamp
//   Material textures and render targets read by index: bindless (ResourceDescriptorHeap)

#include "pbrShaderStructures.h"

static const float PI = 3.14159265358979f;
static const float TWO_PI = 6.28318530717959f;
static const float INV_PI = 0.31830988618379f;

ConstantBuffer<FrameConstants> frame : register(b0);

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

bool IsOnScreen(uint2 pixel)
{
    return all(pixel < (uint2)frame.resolution);
}

float2 PixelCentreUV(uint2 pixel)
{
    return (pixel + 0.5f) * frame.inverse_resolution;
}

// A render target read through its bindless index (uniform across the dispatch)
Texture2D<float4> BindlessTarget(uint index)
{
    return ResourceDescriptorHeap[index];
}

// ---------------------------------------------------------------------------------------------------------------------
// Packing

// Visibility buffer: x = instance + 1 (0 = nothing drawn), y = meshlet << 7 | triangle
uint2 PackVisibility(uint instance, uint meshlet, uint triangle_index)
{
    return uint2(instance + 1, (meshlet << 7) | triangle_index);
}

// Meshlet triangles: three 8 bit vertex indices
uint3 UnpackMeshletTriangle(uint packed)
{
    return uint3(packed & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF);
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
struct Guide
{
    float depth;
    float3 normal;
    float roughness;
};

uint2 PackGuide(float linear_depth, float3 normal, float roughness)
{
    uint2 q = (uint2)(saturate(EncodeOctahedral(normal)) * 4095.0f + 0.5f);
    return uint2(asuint(linear_depth), q.x | (q.y << 12) | ((uint)(saturate(roughness) * 255.0f + 0.5f) << 24));
}

Guide UnpackGuide(uint2 g)
{
    Guide guide;
    guide.depth = asfloat(g.x);
    guide.normal = DecodeOctahedral(float2(g.y & 0xFFF, (g.y >> 12) & 0xFFF) / 4095.0f);
    guide.roughness = (g.y >> 24) / 255.0f;
    return guide;
}

// Whether two guide samples see the same surface (temporal reprojection, neighbourhood statistics)
bool SameSurface(Guide a, Guide b)
{
    return b.depth > 0.0f && abs(a.depth - b.depth) < 0.05f * a.depth + 0.01f && dot(a.normal, b.normal) > 0.9f;
}

float3 SRGBToLinear(float3 c)
{
    return select(c <= 0.04045f, c / 12.92f, pow((max(c, 0.0f) + 0.055f) / 1.055f, 2.4f));
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

uint PixelHash(uint2 pixel, uint seed)
{
    return PCGHash(pixel.x + PCGHash(pixel.y + PCGHash(seed)));
}

// PCG: a linear congruential state, permuted for output
struct RNG
{
    uint state;

    float Next()
    {
        uint word = PCGHash(state);
        state = state * 747796405u + 2891336453u;
        return (word >> 8) * (1.0f / 16777216.0f);
    }

    float2 Next2()
    {
        return float2(Next(), Next());
    }

    float4 Next4()
    {
        return float4(Next2(), Next2());
    }
};

RNG CreateRNG(uint2 pixel, uint frame_index, uint stream)
{
    RNG rng;
    rng.state = PixelHash(pixel, frame_index + PCGHash(stream));
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
    return float2(atan2(d.z, d.x) * (0.5f * INV_PI) + 0.5f, acos(clamp(d.y, -1.0f, 1.0f)) * INV_PI);
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

// Sun disk radiance along a direction (seen directly or by mirror reflections)
float3 SunDisk(float3 direction)
{
    return dot(direction, frame.sun_direction.xyz) >= frame.sun_direction.w ? frame.sun_disk_radiance.rgb : 0.0f;
}

// What a ray that leaves the scene sees: the sky (and the sun's disk, where nothing samples the sun as a light)
float3 MissRadiance(float3 direction, bool include_sun_disk)
{
    float3 radiance = HasFlag(PBR_FLAG_SKY_LIGHTING) ? SampleSky(direction) : 0.0f;
    return radiance + (include_sun_disk && HasFlag(PBR_FLAG_SUN_DISK) ? SunDisk(direction) : 0.0f);
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

// Reverse Z, infinite far plane: depth = near / view z
float LinearDepth(float depth)
{
    return frame.camera_position.w / max(depth, 1e-20f);
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    return frame.camera_position.xyz + ViewToWorldDirection(ViewRayDirection(uv) * LinearDepth(depth));
}

// Direction of the primary ray through a screen position (jittered like the rasterised frame)
float3 CameraRayDirection(float2 uv)
{
    return normalize(ViewToWorldDirection(ViewRayDirection(uv)));
}

// Screen position of a world space point (x < 0 when it is behind the camera)
float2 ProjectToUV(float4 position, float4x4 view_projection)
{
    float4 clip = mul(position, view_projection);
    return clip.w > 0.0f ? clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f : -1.0f;
}

#endif
