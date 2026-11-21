#ifndef PBR_SURFACE_HLSLI
#define PBR_SURFACE_HLSLI

// Geometry fetch, material evaluation (bindless textures), the BRDF and ray queries, shared by the rasterised path and the
// path tracer so both shade every surface identically.

#include "pbrCommon.hlsli"

// Emissive triangle light sampling table (passes that sample emissive light bind it at t15)
StructuredBuffer<EmissiveTriangle> EmissiveTriangles : register(t15);

// ---------------------------------------------------------------------------------------------------------------------
// Bindless material textures

Texture2D<float4> MaterialTexture(uint index)
{
    return ResourceDescriptorHeap[NonUniformResourceIndex(index)];
}

// Texture lookups use explicit gradients (visibility buffer resolve) or an explicit level (ray hits)
struct TextureLookup
{
    float2 uv;
    float2 ddx_uv;
    float2 ddy_uv;
    float lod;
    bool gradients;

    float4 Sample(uint texture_index)
    {
        Texture2D<float4> t = MaterialTexture(texture_index);
        if (gradients)
        {
            return t.SampleGrad(AnisotropicSampler, uv, ddx_uv, ddy_uv);
        }
        // An explicit level: trilinear (anisotropic filtering of an explicit level is implementation defined)
        float w, h;
        t.GetDimensions(w, h);
        return t.SampleLevel(TrilinearSampler, uv, max(lod + 0.5f * log2(w * h), 0.0f));
    }
};

TextureLookup LookupWithGradients(float2 uv, float2 ddx_uv, float2 ddy_uv)
{
    TextureLookup l;
    l.uv = uv;
    l.ddx_uv = ddx_uv * exp2(frame.texture_lod_bias);
    l.ddy_uv = ddy_uv * exp2(frame.texture_lod_bias);
    l.lod = 0.0f;
    l.gradients = true;
    return l;
}

// 'lod' is log2 of the texture coordinate footprint (uv units) of the lookup; the texture size is added per texture
TextureLookup LookupWithLevel(float2 uv, float lod)
{
    TextureLookup l;
    l.uv = uv;
    l.ddx_uv = 0.0f;
    l.ddy_uv = 0.0f;
    l.lod = lod + frame.texture_lod_bias;
    l.gradients = false;
    return l;
}

// ---------------------------------------------------------------------------------------------------------------------
// Geometry

struct SurfaceGeometry
{
    float3 position;
    float3 geometric_normal;    // Faces the viewer (flipped for back faces)
    float3 normal;              // Interpolated, faces the viewer
    float4 tangent;             // xyz world, w = bitangent sign
    float2 uv;
    bool front_face;
    float uv_area_ratio;        // UV area / world area of the triangle (texture level of detail for ray hits)
};

struct TriangleVertices
{
    Vertex v0;
    Vertex v1;
    Vertex v2;
};

TriangleVertices FetchMeshTriangle(Mesh mesh, uint primitive)
{
    uint base = mesh.index_offset + primitive * 3;
    TriangleVertices t;
    t.v0 = Vertices[mesh.vertex_offset + Indices[base + 0]];
    t.v1 = Vertices[mesh.vertex_offset + Indices[base + 1]];
    t.v2 = Vertices[mesh.vertex_offset + Indices[base + 2]];
    return t;
}

TriangleVertices FetchMeshletTriangle(uint meshlet_index, uint triangle_index)
{
    Meshlet meshlet = Meshlets[meshlet_index];
    uint packed = MeshletTriangles[meshlet.triangle_offset + triangle_index];
    TriangleVertices t;
    t.v0 = Vertices[MeshletVertices[meshlet.vertex_offset + (packed & 0xFF)]];
    t.v1 = Vertices[MeshletVertices[meshlet.vertex_offset + ((packed >> 8) & 0xFF)]];
    t.v2 = Vertices[MeshletVertices[meshlet.vertex_offset + ((packed >> 16) & 0xFF)]];
    return t;
}

// Interpolates a triangle at barycentrics (b1, b2) in world space. view_direction points from the eye to the surface.
SurfaceGeometry InterpolateSurface(TriangleVertices t, Instance instance, float2 barycentrics, float3 view_direction)
{
    float3 b = float3(1.0f - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    float3x3 world3 = (float3x3)instance.world;
    float3x3 normal3 = (float3x3)instance.normal_matrix;

    float3 p0 = mul(float4(t.v0.position, 1.0f), instance.world).xyz;
    float3 p1 = mul(float4(t.v1.position, 1.0f), instance.world).xyz;
    float3 p2 = mul(float4(t.v2.position, 1.0f), instance.world).xyz;

    SurfaceGeometry g;
    g.position = p0 * b.x + p1 * b.y + p2 * b.z;
    float3 e1 = p1 - p0;
    float3 e2 = p2 - p0;
    float3 cross_e = cross(e1, e2);
    float world_area = length(cross_e);
    g.geometric_normal = world_area > 0.0f ? cross_e / world_area : float3(0, 1, 0);
    g.normal = normalize(mul(t.v0.normal * b.x + t.v1.normal * b.y + t.v2.normal * b.z, normal3));
    float3 tangent = mul(t.v0.tangent.xyz * b.x + t.v1.tangent.xyz * b.y + t.v2.tangent.xyz * b.z, world3);
    g.tangent = float4(dot(tangent, tangent) > 0.0f ? normalize(tangent) : 0.0f, t.v0.tangent.w);
    g.uv = t.v0.uv * b.x + t.v1.uv * b.y + t.v2.uv * b.z;

    float2 uv_e1 = t.v1.uv - t.v0.uv;
    float2 uv_e2 = t.v2.uv - t.v0.uv;
    float uv_area = abs(uv_e1.x * uv_e2.y - uv_e1.y * uv_e2.x);
    g.uv_area_ratio = uv_area / max(world_area, 1e-12f);

    // Front faces wind clockwise as seen by the viewer (D3D convention), so cross(e1, e2) is their outward normal;
    // mirrored instances reverse the winding. Shading uses the side facing the viewer.
    if ((instance.flags & PBR_INSTANCE_MIRRORED) != 0)
    {
        g.geometric_normal = -g.geometric_normal;
        g.tangent.w = -g.tangent.w; // Mirroring also flips the bitangent
    }
    g.front_face = dot(g.geometric_normal, view_direction) < 0.0f;
    if (!g.front_face)
    {
        g.geometric_normal = -g.geometric_normal;
    }
    // Keep the interpolated normal in the same hemisphere as the geometric normal
    if (dot(g.normal, g.geometric_normal) < 0.0f)
    {
        g.normal = -g.normal;
    }
    return g;
}

// ---------------------------------------------------------------------------------------------------------------------
// Materials

struct MaterialSample
{
    float3 albedo;      // Base colour (linear)
    float metallic;
    float roughness;    // Perceptual
    float3 normal;      // Shading normal (world)
    float3 emissive;
    float ao;
    float opacity;
};

float SampleOpacity(Material m, TextureLookup lookup)
{
    float alpha = m.base_colour.a;
    if (m.opacity_texture != PBR_NO_TEXTURE)
    {
        alpha *= lookup.Sample(m.opacity_texture).r;
    }
    else if (m.base_colour_texture != PBR_NO_TEXTURE)
    {
        alpha *= lookup.Sample(m.base_colour_texture).a;
    }
    return alpha;
}

bool PassesAlphaTest(Material m, TextureLookup lookup)
{
    if ((m.flags & PBR_MATERIAL_ALPHA_MASK) == 0 || !HasFlag(PBR_FLAG_ALPHA_TEST))
    {
        return true;
    }
    return SampleOpacity(m, lookup) >= m.alpha_cutoff;
}

MaterialSample EvaluateMaterial(Material m, SurfaceGeometry g, TextureLookup lookup)
{
    MaterialSample s;
    s.albedo = m.base_colour.rgb;
    s.metallic = m.metallic;
    s.roughness = m.roughness;
    s.normal = g.normal;
    s.emissive = m.emissive;
    s.ao = 1.0f;
    s.opacity = 1.0f;
    bool textures = HasFlag(PBR_FLAG_TEXTURES);

    if (textures && m.base_colour_texture != PBR_NO_TEXTURE)
    {
        float4 c = lookup.Sample(m.base_colour_texture);
        s.albedo *= (m.flags & PBR_MATERIAL_BASE_COLOUR_SRGB) ? SRGBToLinear(c.rgb) : c.rgb;
    }
    if (textures && m.metal_rough_texture != PBR_NO_TEXTURE)
    {
        float4 mr = lookup.Sample(m.metal_rough_texture);
        if (m.flags & PBR_MATERIAL_LEGACY_SPECULAR)
        {
            // A Phong specular intensity map: shinier where brighter (dielectric)
            float spec = dot(mr.rgb, 1.0f / 3.0f);
            s.roughness = saturate(lerp(1.0f, 0.35f, sqrt(spec)) * s.roughness / 0.5f);
            s.metallic = 0.0f;
        }
        else
        {
            s.roughness *= mr.g;
            s.metallic *= mr.b;
        }
    }
    if (textures && m.occlusion_texture != PBR_NO_TEXTURE)
    {
        s.ao = lerp(1.0f, lookup.Sample(m.occlusion_texture).r, m.occlusion_strength);
    }
    if (textures && m.emissive_texture != PBR_NO_TEXTURE)
    {
        float3 e = lookup.Sample(m.emissive_texture).rgb;
        s.emissive *= (m.flags & PBR_MATERIAL_EMISSIVE_SRGB) ? SRGBToLinear(e) : e;
    }
    s.emissive *= HasFlag(PBR_FLAG_EMISSIVE) ? frame.emissive_scale : 0.0f;

    if (textures && HasFlag(PBR_FLAG_NORMAL_MAPS) && m.normal_texture != PBR_NO_TEXTURE && dot(g.tangent.xyz, g.tangent.xyz) > 0.0f)
    {
        float2 xy = lookup.Sample(m.normal_texture).xy * 2.0f - 1.0f;
        xy *= m.normal_scale;
        // Reconstructed z works for two and three channel normal maps alike
        float3 tn = float3(xy, sqrt(saturate(1.0f - dot(xy, xy))));
        // The tangent frame of the front face; back faces (double sided) use the front's normal mirrored through the
        // surface plane, so a thin leaf looks the same lit from either side
        float3 n_front = g.front_face ? g.normal : -g.normal;
        float3 t = normalize(g.tangent.xyz - n_front * dot(n_front, g.tangent.xyz));
        float3 bt = cross(n_front, t) * g.tangent.w;
        float3 n = normalize(tn.x * t + tn.y * bt + tn.z * n_front);
        s.normal = g.front_face ? n : normalize(n - 2.0f * dot(n, n_front) * n_front);
    }

    // Overrides for experiments
    if (frame.albedo_override.x >= 0.0f) s.albedo = frame.albedo_override;
    if (frame.roughness_override >= 0.0f) s.roughness = frame.roughness_override;
    if (frame.metallic_override >= 0.0f) s.metallic = frame.metallic_override;
    if (frame.debug_view == PBR_DEBUG_WHITE_FURNACE)
    {
        s.albedo = 1.0f;
        s.metallic = 0.0f;
        s.emissive = 0.0f;
    }
    s.roughness = clamp(s.roughness, frame.roughness_min, 1.0f);
    s.metallic = saturate(s.metallic);
    return s;
}

// ---------------------------------------------------------------------------------------------------------------------
// BRDF: Lambert diffuse + GGX specular (height correlated Smith visibility, Schlick Fresnel), metallic workflow

float3 SpecularF0(MaterialSample s)
{
    return lerp(0.04f, s.albedo, s.metallic);
}

float3 DiffuseColour(MaterialSample s)
{
    return s.albedo * (1.0f - s.metallic);
}

float3 FresnelSchlick(float3 f0, float v_dot_h)
{
    float f = pow(1.0f - saturate(v_dot_h), 5.0f);
    return f0 + (1.0f - f0) * f;
}

float D_GGX(float n_dot_h, float alpha)
{
    float a2 = alpha * alpha;
    float d = (n_dot_h * a2 - n_dot_h) * n_dot_h + 1.0f;
    return a2 / (PI * d * d);
}

// Height correlated Smith visibility: G2 / (4 n.l n.v)
float V_SmithGGXCorrelated(float n_dot_v, float n_dot_l, float alpha)
{
    float a2 = alpha * alpha;
    float gv = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0f - a2) + a2);
    float gl = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0f - a2) + a2);
    return 0.5f / max(gv + gl, 1e-8f);
}

float SmithG1(float n_dot_v, float alpha)
{
    float a2 = alpha * alpha;
    return 2.0f * n_dot_v / (n_dot_v + sqrt(a2 + (1.0f - a2) * n_dot_v * n_dot_v));
}

// Directional albedo of the specular lobe (split sum approximation, Karis "Physically Based Shading on Mobile")
float3 SpecularAlbedo(float3 f0, float roughness, float n_dot_v)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * n_dot_v)) * r.x + r.y;
    float2 ab = float2(-1.04f, 1.04f) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

// Diffuse and specular reflectance times n.l (radiance from direction l to v, per unit incoming radiance)
void EvaluateBRDF(MaterialSample s, float3 v, float3 l, out float3 diffuse, out float3 specular)
{
    float3 n = s.normal;
    float n_dot_l = dot(n, l);
    float n_dot_v = max(dot(n, v), 1e-4f);
    diffuse = 0.0f;
    specular = 0.0f;
    if (n_dot_l <= 0.0f)
    {
        return;
    }
    float3 h = normalize(v + l);
    float n_dot_h = saturate(dot(n, h));
    float v_dot_h = saturate(dot(v, h));
    float alpha = s.roughness * s.roughness;
    float3 f0 = SpecularF0(s);
    float3 f = FresnelSchlick(f0, v_dot_h);
    specular = f * D_GGX(n_dot_h, alpha) * V_SmithGGXCorrelated(n_dot_v, n_dot_l, alpha) * n_dot_l;
    // Energy left to the diffuse layer after specular reflection
    diffuse = DiffuseColour(s) * INV_PI * n_dot_l * (1.0f - SpecularAlbedo(f0, s.roughness, n_dot_v));
}

// GGX visible normal sampling (Heitz 2018), in the local frame of n (z up)
float3 SampleGGXVNDF(float3 v_local, float alpha, float2 u)
{
    float3 vh = normalize(float3(alpha * v_local.x, alpha * v_local.y, v_local.z));
    float lensq = vh.x * vh.x + vh.y * vh.y;
    float3 t1 = lensq > 0.0f ? float3(-vh.y, vh.x, 0.0f) * rsqrt(lensq) : float3(1.0f, 0.0f, 0.0f);
    float3 t2 = cross(vh, t1);
    float r = sqrt(u.x);
    float phi = TWO_PI * u.y;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi);
    float s = 0.5f * (1.0f + vh.z);
    p2 = (1.0f - s) * sqrt(max(1.0f - p1 * p1, 0.0f)) + s * p2;
    float3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0f, 1.0f - p1 * p1 - p2 * p2)) * vh;
    return normalize(float3(alpha * nh.x, alpha * nh.y, max(0.0f, nh.z)));
}

// ---------------------------------------------------------------------------------------------------------------------
// Ray queries (alpha tested geometry reports candidates, tested against its opacity texture)

struct RayHit
{
    bool hit;
    float t;
    uint instance;
    uint primitive;
    float2 barycentrics;
};

float3 OffsetRayOrigin(float3 position, float3 geometric_normal)
{
    // Scaled with the distance from the origin (floating point precision) and the scene's units
    float scale = frame.ray_offset * (1.0f + max(max(abs(position.x), abs(position.y)), abs(position.z)) * 0.0001f);
    return position + geometric_normal * scale;
}

// Runs for every candidate hit of alpha tested geometry, so it only fetches texture coordinates: the texture level comes
// from the mesh's average texel density and the instance's scale instead of the triangle's world space area
bool CandidatePassesAlphaTest(uint instance_index, uint primitive, float2 barycentrics, float t, float cone_spread)
{
    Instance instance = Instances[instance_index];
    Mesh mesh = Meshes[instance.mesh];
    Material material = Materials[mesh.material];
    uint base = mesh.index_offset + primitive * 3;
    float2 uv0 = Vertices[mesh.vertex_offset + Indices[base + 0]].uv;
    float2 uv1 = Vertices[mesh.vertex_offset + Indices[base + 1]].uv;
    float2 uv2 = Vertices[mesh.vertex_offset + Indices[base + 2]].uv;
    float2 uv = uv0 * (1.0f - barycentrics.x - barycentrics.y) + uv1 * barycentrics.x + uv2 * barycentrics.y;
    float scale_squared = max(dot(instance.world[0].xyz, instance.world[0].xyz), 1e-12f);
    float width = max(cone_spread * t, 1e-6f);
    float lod = 0.5f * log2(max(mesh.uv_density / scale_squared, 1e-12f) * width * width) + frame.sky_parameters.w;
    return PassesAlphaTest(material, LookupWithLevel(uv, lod));
}

// The alpha tested candidates a ray may test before it treats the rest as solid (bounds the cost of rays through foliage:
// every candidate interrupts traversal). The path tracer passes NO_ALPHA_LIMIT.
static const uint NO_ALPHA_LIMIT = 0xFFFFFFFFu;

RayHit TraceClosest(float3 origin, float3 direction, float t_max, float cone_spread, uint max_alpha_tests)
{
    uint alpha_tests = 0;
    RayQuery<RAY_FLAG_NONE> q;
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = t_max;
    q.TraceRayInline(SceneBVH, RAY_FLAG_NONE, 0xFF, ray);
    while (q.Proceed())
    {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (alpha_tests++ >= max_alpha_tests || CandidatePassesAlphaTest(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(), q.CandidateTriangleRayT(), cone_spread))
            {
                q.CommitNonOpaqueTriangleHit();
            }
        }
    }
    RayHit hit;
    hit.hit = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    hit.t = hit.hit ? q.CommittedRayT() : t_max;
    hit.instance = hit.hit ? q.CommittedInstanceID() : 0;
    hit.primitive = hit.hit ? q.CommittedPrimitiveIndex() : 0;
    hit.barycentrics = hit.hit ? q.CommittedTriangleBarycentrics() : 0.0f;
    return hit;
}

bool TraceVisibility(float3 origin, float3 direction, float t_max, float cone_spread, uint max_alpha_tests)
{
    uint alpha_tests = 0;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = t_max;
    q.TraceRayInline(SceneBVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, ray);
    while (q.Proceed())
    {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (alpha_tests++ >= max_alpha_tests || CandidatePassesAlphaTest(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(), q.CandidateTriangleRayT(), cone_spread))
            {
                q.CommitNonOpaqueTriangleHit();
            }
        }
    }
    return q.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
}

// Surface and material at a ray hit; cone_width is the ray cone's width at the hit (texture level of detail)
void ResolveHit(RayHit hit, float3 direction, float cone_width, out SurfaceGeometry g, out MaterialSample s)
{
    Instance instance = Instances[hit.instance];
    Mesh mesh = Meshes[instance.mesh];
    Material material = Materials[mesh.material];
    TriangleVertices tv = FetchMeshTriangle(mesh, hit.primitive);
    g = InterpolateSurface(tv, instance, hit.barycentrics, direction);
    float cos_term = max(abs(dot(direction, g.geometric_normal)), 0.05f);
    float width = max(cone_width, 1e-6f) / sqrt(cos_term);
    float lod = 0.5f * log2(max(g.uv_area_ratio, 1e-12f) * width * width);
    s = EvaluateMaterial(material, g, LookupWithLevel(g.uv, lod));
}

// ---------------------------------------------------------------------------------------------------------------------
// Emissive triangle light sampling (next event estimation of emissive surfaces)

// Candidates per emissive light sample at primary surfaces and path tracer vertices (pbr.light.emissive_candidates)
uint EmissiveCandidates()
{
    return (uint)frame.emissive_lights.w;
}

bool EmissiveSamplingEnabled()
{
    return frame.emissive_lights.z > 0.0f && frame.emissive_lights.x > 0.0f && HasFlag(PBR_FLAG_EMISSIVE);
}

// Probability density (per unit area) of sampling a point on an emissive surface with this material: triangles are
// weighted by luminance(emission factor) * area, so the density only depends on the material
float EmissiveAreaPdf(Material material)
{
    return Luminance(material.emissive) / max(frame.emissive_lights.y, 1e-20f);
}

struct LightSample
{
    float3 position;
    float3 normal;
    float3 radiance;        // Emitted (towards either side); the emission factor only until EmissiveSampleRadiance
    float pdf_area;
    float2 uv;
    uint material;
};

// u: column, alias test and two coordinates of the point on the triangle
LightSample SampleEmissiveTriangle(float4 u)
{
    uint count = (uint)frame.emissive_lights.x;
    uint index = min((uint)(u.x * count), count - 1);
    EmissiveTriangle e = EmissiveTriangles[index];
    if (u.y >= e.probability)
    {
        e = EmissiveTriangles[e.alias];
    }
    Instance instance = Instances[e.instance];
    Mesh mesh = Meshes[instance.mesh];
    Material material = Materials[mesh.material];
    TriangleVertices tv = FetchMeshTriangle(mesh, e.primitive);
    float3 p0 = mul(float4(tv.v0.position, 1.0f), instance.world).xyz;
    float3 p1 = mul(float4(tv.v1.position, 1.0f), instance.world).xyz;
    float3 p2 = mul(float4(tv.v2.position, 1.0f), instance.world).xyz;
    // Uniform point on the triangle
    float su = sqrt(u.z);
    float b1 = su * (1.0f - u.w);
    float b2 = su * u.w;
    float b0 = 1.0f - su;
    LightSample ls;
    ls.position = p0 * b0 + p1 * b1 + p2 * b2;
    float3 n = cross(p1 - p0, p2 - p0);
    ls.normal = normalize(n);
    ls.uv = tv.v0.uv * b0 + tv.v1.uv * b1 + tv.v2.uv * b2;
    ls.material = mesh.material;
    ls.radiance = material.emissive * frame.emissive_scale;
    ls.pdf_area = EmissiveAreaPdf(material);
    return ls;
}

// The sampled point's emitted radiance, with the emissive texture (left out of the candidates' contribution estimates)
float3 EmissiveSampleRadiance(LightSample ls)
{
    Material material = Materials[ls.material];
    float3 emission = ls.radiance;
    if (HasFlag(PBR_FLAG_TEXTURES) && material.emissive_texture != PBR_NO_TEXTURE)
    {
        float3 t = LookupWithLevel(ls.uv, -6.0f).Sample(material.emissive_texture).rgb;
        emission *= (material.flags & PBR_MATERIAL_EMISSIVE_SRGB) ? SRGBToLinear(t) : t;
    }
    return emission;
}

// Pdf (solid angle) of sampling direction l with the BRDF lobe mixture the path tracer uses
float BRDFPdf(MaterialSample s, float3 v, float3 l, float p_specular)
{
    float3 n = s.normal;
    float n_dot_l = dot(n, l);
    float n_dot_v = max(dot(n, v), 1e-4f);
    if (n_dot_l <= 0.0f)
    {
        return 0.0f;
    }
    float alpha = s.roughness * s.roughness;
    float3 h = normalize(v + l);
    // Visible normal density reflected: G1(v) D(h) / (4 n.v)
    float pdf_specular = SmithG1(n_dot_v, alpha) * D_GGX(saturate(dot(n, h)), alpha) / (4.0f * n_dot_v);
    float pdf_diffuse = n_dot_l * INV_PI;
    return p_specular * pdf_specular + (1.0f - p_specular) * pdf_diffuse;
}

// Light from emissive surfaces reaching a surface towards v, from one shadow ray. Resampled importance sampling (Talbot et
// al. 2005): 'candidates' (at least 1) are drawn from the power weighted triangle table and one is kept in
// proportion to its unshadowed contribution (emission factor * cos * cos / distance^2; the texture is only read for
// the kept one). Lights near the surface and facing it are
// then chosen far more often than the table's global power weighting would, which removes most of the variance of scenes
// with many small lights (streets of lamps), while the estimate stays unbiased.
// Returns false when the sample does not contribute. 'diffuse' and 'specular' are the BRDF weighted contributions
// (radiance), 'irradiance' the sample's irradiance estimate and 'l' its direction. 'pdf_solid' is the triangle table's
// density per unit solid angle of l: rays that hit emitters weight themselves with the same density (EmissiveAreaPdf), so
// the multiple importance sampling weights of both strategies still sum to one.
bool EmissiveLight(SurfaceGeometry g, MaterialSample s, float3 v, inout RNG rng, uint candidates, uint max_alpha_tests, out float3 diffuse, out float3 specular, out float3 irradiance, out float pdf_solid, out float3 l)
{
    diffuse = 0.0f;
    specular = 0.0f;
    irradiance = 0.0f;
    pdf_solid = 0.0f;
    l = 0.0f;
    candidates = max(candidates, 1u);
    LightSample selected = (LightSample)0;
    float selected_target = 0.0f;
    float weight_sum = 0.0f;
    for (uint i = 0; i < candidates; i++)
    {
        LightSample ls = SampleEmissiveTriangle(float4(rng.Next2(), rng.Next2()));
        float3 to_light = ls.position - g.position;
        float distance_squared = max(dot(to_light, to_light), 1e-8f);
        float3 direction = to_light * rsqrt(distance_squared);
        float cos_light = abs(dot(ls.normal, direction)); // Emitters radiate from both sides
        float cos_surface = dot(s.normal, direction);
        float target = 0.0f;
        if (cos_light > 1e-4f && cos_surface > 0.0f && dot(g.geometric_normal, direction) > 0.0f && ls.pdf_area > 0.0f)
        {
            target = Luminance(ls.radiance) * cos_light * cos_surface / distance_squared;
        }
        float w = target / max(ls.pdf_area, 1e-20f);
        weight_sum += w;
        if (w > 0.0f && rng.Next() * weight_sum < w)
        {
            selected = ls;
            selected_target = target;
        }
    }
    if (selected_target <= 0.0f)
    {
        return false;
    }
    float3 to_light = selected.position - g.position;
    float distance_squared = dot(to_light, to_light);
    float distance = sqrt(distance_squared);
    l = to_light / max(distance, 1e-6f);
    float cos_light = abs(dot(selected.normal, l));
    pdf_solid = selected.pdf_area * distance_squared / cos_light;
    float3 origin = OffsetRayOrigin(g.position, g.geometric_normal);
    float t_max = length(selected.position - origin) * 0.999f - frame.ray_offset;
    if (t_max <= 0.0f || !TraceVisibility(origin, l, t_max, 0.0f, max_alpha_tests))
    {
        return false;
    }
    // The resampled sample's density is replaced by its unbiased contribution weight W = weight_sum / (M target)
    float pdf_resampled = candidates * selected_target / weight_sum * distance_squared / cos_light;
    float3 radiance = EmissiveSampleRadiance(selected);
    float3 brdf_diffuse, brdf_specular;
    EvaluateBRDF(s, v, l, brdf_diffuse, brdf_specular);
    diffuse = brdf_diffuse * radiance / pdf_resampled;
    specular = brdf_specular * radiance / pdf_resampled;
    irradiance = radiance * dot(s.normal, l) / pdf_resampled;
    return true;
}

// Direct sunlight at a surface (soft shadow: a random direction within the sun's disk)
float3 DirectSun(SurfaceGeometry g, MaterialSample s, float3 v, float2 u, bool shadows, float cone_spread, uint max_alpha_tests)
{
    float3 l = ToWorld(SampleCone(u, frame.sun_direction.w), frame.sun_direction.xyz);
    if (dot(g.geometric_normal, l) <= 0.0f || dot(s.normal, l) <= 0.0f)
    {
        return 0.0f;
    }
    if (shadows && !TraceVisibility(OffsetRayOrigin(g.position, g.geometric_normal), l, frame.max_ray_distance, cone_spread, max_alpha_tests))
    {
        return 0.0f;
    }
    float3 diffuse, specular;
    EvaluateBRDF(s, v, l, diffuse, specular);
    return (diffuse + specular) * frame.sun_illuminance.rgb;
}

#endif
