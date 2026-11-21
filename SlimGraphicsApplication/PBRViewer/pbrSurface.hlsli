#ifndef PBR_SURFACE_HLSLI
#define PBR_SURFACE_HLSLI

// Geometry fetch, material evaluation (bindless textures), the BRDF, ray queries and light sampling, shared by the real time
// renderer and the path tracer so both shade every surface identically.

#include "pbrCommon.hlsli"

// Emissive triangle light sampling table (passes that sample emissive light bind it at t15)
StructuredBuffer<EmissiveTriangle> EmissiveTriangles : register(t15);

// ---------------------------------------------------------------------------------------------------------------------
// Bindless material textures

// Texture lookups use explicit gradients (visibility buffer resolve, pixel shaders) or an explicit level (ray hits)
struct TextureLookup
{
    float2 uv;
    float2 ddx_uv;
    float2 ddy_uv;
    float lod;          // log2 of the lookup's footprint in texture coordinates (the texture size is added per texture)
    bool gradients;

    float4 Sample(uint texture_index)
    {
        Texture2D<float4> t = ResourceDescriptorHeap[NonUniformResourceIndex(texture_index)];
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
    float scale = exp2(frame.texture_lod_bias);
    TextureLookup l = { uv, ddx_uv * scale, ddy_uv * scale, 0.0f, true };
    return l;
}

TextureLookup LookupWithLevel(float2 uv, float lod)
{
    TextureLookup l = { uv, float2(0.0f, 0.0f), float2(0.0f, 0.0f), lod + frame.texture_lod_bias, false };
    return l;
}

// The level of a ray hit's lookup: a cone 'width' wide over a surface with 'uv_per_area' texture coordinate area per unit area
float RayConeLod(float uv_per_area, float width)
{
    width = max(width, 1e-6f);
    return 0.5f * log2(max(uv_per_area, 1e-12f) * width * width);
}

// ---------------------------------------------------------------------------------------------------------------------
// Geometry

struct TriangleVertices
{
    Vertex v0;
    Vertex v1;
    Vertex v2;
};

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

float3 Barycentrics(float2 b)
{
    return float3(1.0f - b.x - b.y, b.x, b.y);
}

uint3 MeshTriangleIndices(Mesh mesh, uint primitive)
{
    uint base = mesh.index_offset + primitive * 3;
    return mesh.vertex_offset + uint3(Indices[base], Indices[base + 1], Indices[base + 2]);
}

TriangleVertices FetchMeshTriangle(Mesh mesh, uint primitive)
{
    uint3 i = MeshTriangleIndices(mesh, primitive);
    TriangleVertices t = { Vertices[i.x], Vertices[i.y], Vertices[i.z] };
    return t;
}

TriangleVertices FetchMeshletTriangle(uint meshlet_index, uint triangle_index)
{
    Meshlet meshlet = Meshlets[meshlet_index];
    uint3 i = meshlet.vertex_offset + UnpackMeshletTriangle(MeshletTriangles[meshlet.triangle_offset + triangle_index]);
    TriangleVertices t = { Vertices[MeshletVertices[i.x]], Vertices[MeshletVertices[i.y]], Vertices[MeshletVertices[i.z]] };
    return t;
}

float3 TransformPoint(float3 p, float4x4 world)
{
    return mul(float4(p, 1.0f), world).xyz;
}

struct WorldTriangle
{
    float3 p0;
    float3 p1;
    float3 p2;
};

WorldTriangle TransformTriangle(TriangleVertices t, Instance instance)
{
    WorldTriangle w = { TransformPoint(t.v0.position, instance.world), TransformPoint(t.v1.position, instance.world), TransformPoint(t.v2.position, instance.world) };
    return w;
}

// Interpolates a triangle at barycentrics (b1, b2) in world space. view_direction points from the eye to the surface.
SurfaceGeometry InterpolateSurface(TriangleVertices t, Instance instance, float2 barycentrics, float3 view_direction)
{
    float3 b = Barycentrics(barycentrics);
    WorldTriangle p = TransformTriangle(t, instance);

    SurfaceGeometry g;
    g.position = p.p0 * b.x + p.p1 * b.y + p.p2 * b.z;
    float3 cross_e = cross(p.p1 - p.p0, p.p2 - p.p0);
    float world_area = length(cross_e);
    g.geometric_normal = world_area > 0.0f ? cross_e / world_area : float3(0.0f, 1.0f, 0.0f);
    g.normal = normalize(mul(t.v0.normal * b.x + t.v1.normal * b.y + t.v2.normal * b.z, (float3x3)instance.normal_matrix));
    float3 tangent = mul(t.v0.tangent.xyz * b.x + t.v1.tangent.xyz * b.y + t.v2.tangent.xyz * b.z, (float3x3)instance.world);
    g.tangent = float4(dot(tangent, tangent) > 0.0f ? normalize(tangent) : 0.0f, t.v0.tangent.w);
    g.uv = t.v0.uv * b.x + t.v1.uv * b.y + t.v2.uv * b.z;
    float2 uv_e1 = t.v1.uv - t.v0.uv;
    float2 uv_e2 = t.v2.uv - t.v0.uv;
    g.uv_area_ratio = abs(uv_e1.x * uv_e2.y - uv_e1.y * uv_e2.x) / max(world_area, 1e-12f);

    // Front faces wind clockwise as seen by the viewer (D3D convention), so cross(e1, e2) is their outward normal;
    // mirrored instances reverse the winding (and the bitangent). Shading uses the side facing the viewer.
    if ((instance.flags & PBR_INSTANCE_MIRRORED) != 0)
    {
        g.geometric_normal = -g.geometric_normal;
        g.tangent.w = -g.tangent.w;
    }
    g.front_face = dot(g.geometric_normal, view_direction) < 0.0f;
    g.geometric_normal = g.front_face ? g.geometric_normal : -g.geometric_normal;
    // Keep the interpolated normal in the same hemisphere as the geometric normal
    g.normal = dot(g.normal, g.geometric_normal) < 0.0f ? -g.normal : g.normal;
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
};

// A colour texel: textures flagged as sRGB values in a UNORM format are decoded here
float3 DecodeColour(float4 texel, Material m, uint srgb_flag)
{
    return (m.flags & srgb_flag) ? SRGBToLinear(texel.rgb) : texel.rgb;
}

bool PassesAlphaTest(Material m, TextureLookup lookup)
{
    if ((m.flags & PBR_MATERIAL_ALPHA_MASK) == 0 || !HasFlag(PBR_FLAG_ALPHA_TEST))
    {
        return true;
    }
    float alpha = m.base_colour.a;
    if (m.opacity_texture != PBR_NO_TEXTURE)
    {
        alpha *= lookup.Sample(m.opacity_texture).r;
    }
    else if (m.base_colour_texture != PBR_NO_TEXTURE)
    {
        alpha *= lookup.Sample(m.base_colour_texture).a;
    }
    return alpha >= m.alpha_cutoff;
}

// Emitted radiance (before frame.emissive_scale)
float3 SampleEmission(Material m, TextureLookup lookup)
{
    bool textured = HasFlag(PBR_FLAG_TEXTURES) && m.emissive_texture != PBR_NO_TEXTURE;
    return m.emissive * (textured ? DecodeColour(lookup.Sample(m.emissive_texture), m, PBR_MATERIAL_EMISSIVE_SRGB) : 1.0f);
}

MaterialSample EvaluateMaterial(Material m, SurfaceGeometry g, TextureLookup lookup)
{
    MaterialSample s;
    s.albedo = m.base_colour.rgb;
    s.metallic = m.metallic;
    s.roughness = m.roughness;
    s.normal = g.normal;
    s.emissive = SampleEmission(m, lookup) * (HasFlag(PBR_FLAG_EMISSIVE) ? frame.emissive_scale : 0.0f);
    s.ao = 1.0f;

    if (HasFlag(PBR_FLAG_TEXTURES))
    {
        if (m.base_colour_texture != PBR_NO_TEXTURE)
        {
            s.albedo *= DecodeColour(lookup.Sample(m.base_colour_texture), m, PBR_MATERIAL_BASE_COLOUR_SRGB);
        }
        if (m.metal_rough_texture != PBR_NO_TEXTURE)
        {
            float4 mr = lookup.Sample(m.metal_rough_texture);
            if (m.flags & PBR_MATERIAL_LEGACY_SPECULAR)
            {
                // A Phong specular intensity map: shinier where brighter (dielectric)
                s.roughness = saturate(lerp(1.0f, 0.35f, sqrt(dot(mr.rgb, 1.0f / 3.0f))) * s.roughness / 0.5f);
                s.metallic = 0.0f;
            }
            else
            {
                s.roughness *= mr.g;
                s.metallic *= mr.b;
            }
        }
        if (m.occlusion_texture != PBR_NO_TEXTURE)
        {
            s.ao = lerp(1.0f, lookup.Sample(m.occlusion_texture).r, m.occlusion_strength);
        }
        if (HasFlag(PBR_FLAG_NORMAL_MAPS) && m.normal_texture != PBR_NO_TEXTURE && dot(g.tangent.xyz, g.tangent.xyz) > 0.0f)
        {
            // Reconstructed z works for two and three channel normal maps alike
            float2 xy = (lookup.Sample(m.normal_texture).xy * 2.0f - 1.0f) * m.normal_scale;
            float3 tn = float3(xy, sqrt(saturate(1.0f - dot(xy, xy))));
            // The tangent frame of the front face; back faces (double sided) use the front's normal mirrored through the
            // surface plane, so a thin leaf looks the same lit from either side
            float3 n_front = g.front_face ? g.normal : -g.normal;
            float3 t = normalize(g.tangent.xyz - n_front * dot(n_front, g.tangent.xyz));
            float3 n = normalize(tn.x * t + tn.y * cross(n_front, t) * g.tangent.w + tn.z * n_front);
            s.normal = g.front_face ? n : normalize(n - 2.0f * dot(n, n_front) * n_front);
        }
    }

    // Overrides for experiments
    s.albedo = frame.albedo_override.x >= 0.0f ? frame.albedo_override : s.albedo;
    s.roughness = frame.roughness_override >= 0.0f ? frame.roughness_override : s.roughness;
    s.metallic = frame.metallic_override >= 0.0f ? frame.metallic_override : s.metallic;
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

// A surface from the G-buffer (see pbrResolve.hlsl)
MaterialSample GBufferMaterial(float4 albedo_metallic, float4 normal_roughness, float ao, float3 emissive)
{
    MaterialSample s;
    s.albedo = albedo_metallic.rgb;
    s.metallic = albedo_metallic.a;
    s.normal = normalize(normal_roughness.xyz);
    s.roughness = normal_roughness.w;
    s.emissive = emissive;
    s.ao = ao;
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
    return f0 + (1.0f - f0) * pow(1.0f - saturate(v_dot_h), 5.0f);
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

float NdotV(MaterialSample s, float3 v)
{
    return max(dot(s.normal, v), 1e-4f);
}

// Directional albedo of the specular lobe (split sum approximation, Karis "Physically Based Shading on Mobile")
float3 SpecularAlbedo(MaterialSample s, float3 v)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = s.roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV(s, v))) * r.x + r.y;
    float2 ab = float2(-1.04f, 1.04f) * a004 + r.zw;
    return SpecularF0(s) * ab.x + ab.y;
}

// Diffuse and specular reflectance times n.l (radiance from direction l to v, per unit incoming radiance)
void EvaluateBRDF(MaterialSample s, float3 v, float3 l, out float3 diffuse, out float3 specular)
{
    float n_dot_l = dot(s.normal, l);
    diffuse = 0.0f;
    specular = 0.0f;
    if (n_dot_l <= 0.0f)
    {
        return;
    }
    float3 h = normalize(v + l);
    float alpha = s.roughness * s.roughness;
    float3 f = FresnelSchlick(SpecularF0(s), dot(v, h));
    specular = f * D_GGX(saturate(dot(s.normal, h)), alpha) * V_SmithGGXCorrelated(NdotV(s, v), n_dot_l, alpha) * n_dot_l;
    // Energy left to the diffuse layer after specular reflection
    diffuse = DiffuseColour(s) * INV_PI * n_dot_l * (1.0f - SpecularAlbedo(s, v));
}

float3 BRDF(MaterialSample s, float3 v, float3 l)
{
    float3 diffuse, specular;
    EvaluateBRDF(s, v, l, diffuse, specular);
    return diffuse + specular;
}

// GGX visible normal sampling (Heitz 2018): a reflection direction around s.normal
float3 SampleSpecularDirection(MaterialSample s, float3 v, float2 u)
{
    float alpha = s.roughness * s.roughness;
    float3 v_local = ToLocal(v, s.normal);
    float3 vh = normalize(float3(alpha * v_local.xy, max(v_local.z, 1e-4f)));
    float lensq = dot(vh.xy, vh.xy);
    float3 t1 = lensq > 0.0f ? float3(-vh.y, vh.x, 0.0f) * rsqrt(lensq) : float3(1.0f, 0.0f, 0.0f);
    float3 t2 = cross(vh, t1);
    float r = sqrt(u.x);
    float phi = TWO_PI * u.y;
    float p1 = r * cos(phi);
    float s2 = 0.5f * (1.0f + vh.z);
    float p2 = (1.0f - s2) * sqrt(max(1.0f - p1 * p1, 0.0f)) + s2 * r * sin(phi);
    float3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0f, 1.0f - p1 * p1 - p2 * p2)) * vh;
    float3 h = ToWorld(normalize(float3(alpha * nh.xy, max(0.0f, nh.z))), s.normal);
    return reflect(-v, h);
}

// Probability of choosing the specular lobe when sampling a direction (their estimated reflectance)
float SpecularLobeProbability(MaterialSample s, float3 v)
{
    float3 specular_albedo = SpecularAlbedo(s, v);
    float specular = Luminance(specular_albedo);
    float diffuse = Luminance(DiffuseColour(s) * (1.0f - specular_albedo));
    return specular / max(specular + diffuse, 1e-6f);
}

// Density (solid angle) of sampling direction l with the lobe mixture (p_specular = 1: visible normals alone)
float BRDFPdf(MaterialSample s, float3 v, float3 l, float p_specular)
{
    float n_dot_l = dot(s.normal, l);
    if (n_dot_l <= 0.0f)
    {
        return 0.0f;
    }
    float alpha = s.roughness * s.roughness;
    float n_dot_v = NdotV(s, v);
    // Visible normal density reflected: G1(v) D(h) / (4 n.v)
    float pdf_specular = SmithG1(n_dot_v, alpha) * D_GGX(saturate(dot(s.normal, normalize(v + l))), alpha) / (4.0f * n_dot_v);
    return lerp(n_dot_l * INV_PI, pdf_specular, p_specular);
}

// Multiple importance sampling weight of a strategy with density 'pdf' against one with 'other_pdf'
float PowerHeuristic(float pdf, float other_pdf)
{
    return pdf * pdf / max(pdf * pdf + other_pdf * other_pdf, 1e-20f);
}

// Scales an indirect contribution down to the firefly clamp's exposed luminance (pbr.light.firefly_clamp, 0 = unbiased)
float3 ClampFirefly(float3 radiance)
{
    float exposed = Luminance(radiance) * frame.exposure;
    return frame.firefly_clamp > 0.0f && exposed > frame.firefly_clamp ? radiance * (frame.firefly_clamp / exposed) : radiance;
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
    // Grows with the distance from the origin (floating point precision)
    float scale = frame.ray_offset * (1.0f + max(max(abs(position.x), abs(position.y)), abs(position.z)) * 0.0001f);
    return position + geometric_normal * scale;
}

// Runs for every candidate hit of alpha tested geometry, so it only fetches texture coordinates: the texture level comes
// from the mesh's average texel density and the instance's scale instead of the triangle's world space area
bool CandidatePassesAlphaTest(uint instance_index, uint primitive, float2 barycentrics, float t, float cone_spread)
{
    Instance instance = Instances[instance_index];
    Mesh mesh = Meshes[instance.mesh];
    uint3 i = MeshTriangleIndices(mesh, primitive);
    float3 b = Barycentrics(barycentrics);
    float2 uv = Vertices[i.x].uv * b.x + Vertices[i.y].uv * b.y + Vertices[i.z].uv * b.z;
    float scale_squared = max(dot(instance.world[0].xyz, instance.world[0].xyz), 1e-12f);
    float lod = RayConeLod(mesh.uv_density / scale_squared, cone_spread * t) + frame.alpha_test_lod_bias;
    return PassesAlphaTest(Materials[mesh.material], LookupWithLevel(uv, lod));
}

// The alpha tested candidates a ray may test before it treats the rest as solid (bounds the cost of rays through foliage:
// every candidate interrupts traversal). The path tracer passes NO_ALPHA_LIMIT.
static const uint NO_ALPHA_LIMIT = 0xFFFFFFFFu;

// 'flags': RAY_FLAG_NONE for the closest hit, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH for visibility
RayHit TraceRay(float3 origin, float3 direction, float t_max, float cone_spread, uint max_alpha_tests, uint flags)
{
    RayDesc ray = { origin, 0.0f, direction, t_max };
    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(SceneBVH, flags, 0xFF, ray);
    uint alpha_tests = 0;
    while (q.Proceed())
    {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            (alpha_tests++ >= max_alpha_tests || CandidatePassesAlphaTest(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(), q.CandidateTriangleRayT(), cone_spread)))
        {
            q.CommitNonOpaqueTriangleHit();
        }
    }
    RayHit hit = (RayHit)0;
    hit.hit = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    hit.t = t_max;
    if (hit.hit)
    {
        hit.t = q.CommittedRayT();
        hit.instance = q.CommittedInstanceID();
        hit.primitive = q.CommittedPrimitiveIndex();
        hit.barycentrics = q.CommittedTriangleBarycentrics();
    }
    return hit;
}

RayHit TraceClosest(float3 origin, float3 direction, float t_max, float cone_spread, uint max_alpha_tests)
{
    return TraceRay(origin, direction, t_max, cone_spread, max_alpha_tests, RAY_FLAG_NONE);
}

bool TraceVisibility(float3 origin, float3 direction, float t_max, float cone_spread, uint max_alpha_tests)
{
    return !TraceRay(origin, direction, t_max, cone_spread, max_alpha_tests, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH).hit;
}

// Surface and material at a ray hit; cone_width is the ray cone's width at the hit (texture level of detail)
void ResolveHit(RayHit hit, float3 direction, float cone_width, out SurfaceGeometry g, out MaterialSample s)
{
    Instance instance = Instances[hit.instance];
    Mesh mesh = Meshes[instance.mesh];
    g = InterpolateSurface(FetchMeshTriangle(mesh, hit.primitive), instance, hit.barycentrics, direction);
    // The cone's footprint stretches on surfaces seen at grazing angles
    float footprint = cone_width / sqrt(max(abs(dot(direction, g.geometric_normal)), 0.05f));
    s = EvaluateMaterial(Materials[mesh.material], g, LookupWithLevel(g.uv, RayConeLod(g.uv_area_ratio, footprint)));
}

// ---------------------------------------------------------------------------------------------------------------------
// Emissive triangle light sampling (next event estimation of emissive surfaces)

bool EmissiveSamplingEnabled()
{
    return frame.emissive_triangle_count > 0 && HasFlag(PBR_FLAG_EMISSIVE);
}

// Density (per unit area) of the light sampling table picking a point of a surface whose material's emission factor has
// this luminance: triangles are weighted by luminance(emission factor) * area, so the density only depends on the material
float EmissiveAreaPdf(float emission)
{
    return emission / max(frame.emissive_weight, 1e-20f);
}

float AreaToSolidAnglePdf(float pdf_area, float distance_squared, float cos_light)
{
    return pdf_area * distance_squared / max(cos_light, 1e-4f);
}

// The light sampling table's density (solid angle) of the emitter a ray hit: the other strategy's weight for emission
// found by rays (multiple importance sampling)
float EmitterHitPdf(RayHit hit, SurfaceGeometry g, float3 direction)
{
    Material material = Materials[Meshes[Instances[hit.instance].mesh].material];
    return AreaToSolidAnglePdf(EmissiveAreaPdf(Luminance(material.emissive)), hit.t * hit.t, abs(dot(g.geometric_normal, direction)));
}

struct LightCandidate
{
    float3 position;
    float3 normal;
    float emission;     // Luminance of the material's emission factor
    float pdf_area;
    float2 uv;
    uint material;
};

// u: column, alias test and two coordinates of the point on the triangle
LightCandidate SampleEmissiveTriangle(float4 u)
{
    uint count = frame.emissive_triangle_count;
    EmissiveTriangle e = EmissiveTriangles[min((uint)(u.x * count), count - 1)];
    if (u.y >= e.probability)
    {
        e = EmissiveTriangles[e.alias];
    }
    Instance instance = Instances[e.instance];
    Mesh mesh = Meshes[instance.mesh];
    TriangleVertices tv = FetchMeshTriangle(mesh, e.primitive);
    WorldTriangle p = TransformTriangle(tv, instance);
    // Uniform point on the triangle
    float su = sqrt(u.z);
    float3 b = float3(1.0f - su, su * (1.0f - u.w), su * u.w);
    LightCandidate c;
    c.position = p.p0 * b.x + p.p1 * b.y + p.p2 * b.z;
    c.normal = normalize(cross(p.p1 - p.p0, p.p2 - p.p0));
    c.uv = tv.v0.uv * b.x + tv.v1.uv * b.y + tv.v2.uv * b.z;
    c.material = mesh.material;
    c.emission = Luminance(Materials[mesh.material].emissive);
    c.pdf_area = EmissiveAreaPdf(c.emission);
    return c;
}

struct EmissiveLightSample
{
    float3 direction;
    float3 diffuse;         // BRDF weighted contributions (radiance towards v)
    float3 specular;
    float3 irradiance;      // The sample's irradiance estimate
    float pdf;              // The light sampling table's density (solid angle) of 'direction', for MIS
};

// Light from emissive surfaces reaching a surface towards v, from one shadow ray. Resampled importance sampling (Talbot et
// al. 2005): 'candidates' are drawn from the power weighted triangle table and one is kept in proportion to its unshadowed
// contribution (emission factor * cos * cos / distance^2; the texture is only read for the kept one). Lights near the
// surface and facing it are then chosen far more often than the table's global power weighting would, which removes most
// of the variance of scenes with many small lights (streets of lamps), while the estimate stays unbiased.
// Rays that hit emitters weight themselves with the table's density (EmitterHitPdf), which is also what 'pdf' reports, so
// the multiple importance sampling weights of both strategies still sum to one. Returns false when nothing contributes.
bool SampleEmissiveLight(SurfaceGeometry g, MaterialSample s, float3 v, inout RNG rng, uint candidates, uint max_alpha_tests, out EmissiveLightSample light)
{
    light = (EmissiveLightSample)0;
    LightCandidate selected = (LightCandidate)0;
    float selected_target = 0.0f;
    float weight_sum = 0.0f;
    candidates = max(candidates, 1u);
    for (uint i = 0; i < candidates; i++)
    {
        LightCandidate c = SampleEmissiveTriangle(rng.Next4());
        float3 to_light = c.position - g.position;
        float distance_squared = max(dot(to_light, to_light), 1e-8f);
        float3 l = to_light * rsqrt(distance_squared);
        float cos_light = abs(dot(c.normal, l)); // Emitters radiate from both sides
        float cos_surface = dot(s.normal, l);
        bool facing = cos_light > 1e-4f && cos_surface > 0.0f && dot(g.geometric_normal, l) > 0.0f && c.pdf_area > 0.0f;
        float target = facing ? c.emission * cos_light * cos_surface / distance_squared : 0.0f;
        float w = target / max(c.pdf_area, 1e-20f);
        weight_sum += w;
        if (w > 0.0f && rng.Next() * weight_sum < w)
        {
            selected = c;
            selected_target = target;
        }
    }
    if (selected_target <= 0.0f)
    {
        return false;
    }
    float3 to_light = selected.position - g.position;
    float distance_squared = dot(to_light, to_light);
    light.direction = to_light * rsqrt(distance_squared);
    float cos_light = abs(dot(selected.normal, light.direction));
    light.pdf = AreaToSolidAnglePdf(selected.pdf_area, distance_squared, cos_light);
    float3 origin = OffsetRayOrigin(g.position, g.geometric_normal);
    float t_max = length(selected.position - origin) * 0.999f - frame.ray_offset;
    if (t_max <= 0.0f || !TraceVisibility(origin, light.direction, t_max, 0.0f, max_alpha_tests))
    {
        return false;
    }
    // The resampled sample's density is replaced by its unbiased contribution weight W = weight_sum / (M target)
    float pdf_resampled = AreaToSolidAnglePdf(candidates * selected_target / weight_sum, distance_squared, cos_light);
    Material material = Materials[selected.material];
    float3 radiance = SampleEmission(material, LookupWithLevel(selected.uv, -6.0f)) * frame.emissive_scale / pdf_resampled;
    float3 brdf_diffuse, brdf_specular;
    EvaluateBRDF(s, v, light.direction, brdf_diffuse, brdf_specular);
    light.diffuse = brdf_diffuse * radiance;
    light.specular = brdf_specular * radiance;
    light.irradiance = radiance * dot(s.normal, light.direction);
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
    return BRDF(s, v, l) * frame.sun_illuminance.rgb;
}

#endif
