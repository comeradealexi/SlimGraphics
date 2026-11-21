// Visibility buffer resolve: per pixel, rebuilds the visible triangle from its instance / meshlet / triangle ids, intersects
// the pixel's camera ray with it for exact barycentrics (and the neighbouring pixels' rays for texture gradients), evaluates
// the material once and writes the G-buffer. Shading happens once per pixel, with no overdraw.

#include "pbrSurface.hlsli"

Texture2D<uint2> Visibility : register(t10);
Texture2D<float> Depth : register(t11);

RWTexture2D<float4> GBufferAlbedoMetallic : register(u0);   // Base colour, metallic
RWTexture2D<float4> GBufferNormalRoughness : register(u1);  // Shading normal, perceptual roughness
RWTexture2D<float4> GBufferGeometry : register(u2);         // Geometric normal (octahedral), ambient occlusion, front face
RWTexture2D<float4> GBufferEmissive : register(u3);         // Emissive radiance
RWTexture2D<float2> Motion : register(u4);                  // Current UV - previous UV
RWTexture2D<uint2> Guide : register(u5);                    // Denoiser guide (PackGuide)

// Barycentrics (b1, b2) of the intersection of a ray with a triangle's plane
float2 RayTriangleBarycentrics(float3 origin, float3 direction, float3 p0, float3 p1, float3 p2)
{
    float3 e1 = p1 - p0;
    float3 e2 = p2 - p0;
    float3 pv = cross(direction, e2);
    float det = dot(e1, pv);
    float inv_det = abs(det) > 1e-20f ? 1.0f / det : 0.0f;
    float3 tv = origin - p0;
    float3 qv = cross(tv, e1);
    return float2(dot(tv, pv), dot(direction, qv)) * inv_det;
}

[numthreads(8, 8, 1)]
void resolve(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= (uint2)frame.resolution))
    {
        return;
    }
    uint2 vis = Visibility[pixel];
    float2 uv = (pixel + 0.5f) * frame.inverse_resolution;
    if (vis.x == 0)
    {
        // Sky: motion of a point at infinity (rotation only)
        float3 direction = normalize(ReconstructWorldPosition(uv, 0.5f) - frame.camera_position.xyz);
        float4 previous_clip = mul(float4(direction, 0.0f), frame.previous_view_projection_no_jitter);
        float4 current_clip = mul(float4(direction, 0.0f), frame.view_projection_no_jitter);
        float2 previous_uv = previous_clip.xy / previous_clip.w * float2(0.5f, -0.5f) + 0.5f;
        float2 current_uv = current_clip.xy / current_clip.w * float2(0.5f, -0.5f) + 0.5f;
        GBufferAlbedoMetallic[pixel] = 0.0f;
        GBufferNormalRoughness[pixel] = 0.0f;
        GBufferGeometry[pixel] = 0.0f;
        GBufferEmissive[pixel] = 0.0f;
        Motion[pixel] = previous_clip.w > 0.0f ? current_uv - previous_uv : 0.0f;
        Guide[pixel] = uint2(0, 0);
        return;
    }

    uint instance_index = vis.x - 1;
    uint meshlet_index = vis.y >> 7;
    uint triangle_index = vis.y & 0x7F;
    Instance instance = Instances[instance_index];
    Material material = Materials[instance.material];
    TriangleVertices tv = FetchMeshletTriangle(meshlet_index, triangle_index);

    float3 p0 = mul(float4(tv.v0.position, 1.0f), instance.world).xyz;
    float3 p1 = mul(float4(tv.v1.position, 1.0f), instance.world).xyz;
    float3 p2 = mul(float4(tv.v2.position, 1.0f), instance.world).xyz;

    float3 origin, direction, direction_x, direction_y;
    CameraRay(pixel + 0.5f, origin, direction);
    CameraRay(pixel + float2(1.5f, 0.5f), origin, direction_x);
    CameraRay(pixel + float2(0.5f, 1.5f), origin, direction_y);
    float2 bary = RayTriangleBarycentrics(origin, direction, p0, p1, p2);
    float2 bary_x = RayTriangleBarycentrics(origin, direction_x, p0, p1, p2);
    float2 bary_y = RayTriangleBarycentrics(origin, direction_y, p0, p1, p2);

    SurfaceGeometry g = InterpolateSurface(tv, instance, bary, direction);
    // Texture coordinate gradients from the barycentric differences between neighbouring pixels
    float2 uv_e1 = tv.v1.uv - tv.v0.uv;
    float2 uv_e2 = tv.v2.uv - tv.v0.uv;
    float2 ddx_uv = (bary_x.x - bary.x) * uv_e1 + (bary_x.y - bary.y) * uv_e2;
    float2 ddy_uv = (bary_y.x - bary.x) * uv_e1 + (bary_y.y - bary.y) * uv_e2;
    MaterialSample s = EvaluateMaterial(material, g, LookupWithGradients(g.uv, ddx_uv, ddy_uv));

    // Motion: the surface point at its previous transform, projected with last frame's camera (both unjittered)
    float3 b = float3(1.0f - bary.x - bary.y, bary.x, bary.y);
    float3 object_position = tv.v0.position * b.x + tv.v1.position * b.y + tv.v2.position * b.z;
    float4 previous_clip = mul(mul(float4(object_position, 1.0f), instance.previous_world), frame.previous_view_projection_no_jitter);
    float4 current_clip = mul(float4(g.position, 1.0f), frame.view_projection_no_jitter);
    float2 previous_uv = previous_clip.xy / previous_clip.w * float2(0.5f, -0.5f) + 0.5f;
    float2 current_uv = current_clip.xy / current_clip.w * float2(0.5f, -0.5f) + 0.5f;

    float linear_depth = mul(float4(g.position, 1.0f), frame.view).z;

    GBufferAlbedoMetallic[pixel] = float4(s.albedo, s.metallic);
    GBufferNormalRoughness[pixel] = float4(s.normal, s.roughness);
    GBufferGeometry[pixel] = float4(EncodeOctahedral(g.geometric_normal), s.ao, g.front_face ? 1.0f : 0.0f);
    GBufferEmissive[pixel] = float4(s.emissive, 0.0f);
    Motion[pixel] = current_uv - previous_uv;
    Guide[pixel] = PackGuide(linear_depth, s.normal, s.roughness);
}
