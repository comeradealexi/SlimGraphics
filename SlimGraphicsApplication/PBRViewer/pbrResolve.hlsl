// Visibility buffer resolve: per pixel, rebuilds the visible triangle from its instance / meshlet / triangle ids, intersects
// the pixel's camera ray with it for exact barycentrics (and the neighbouring pixels' rays for texture gradients), evaluates
// the material once and writes the G-buffer. Shading happens once per pixel, with no overdraw.

#include "pbrSurface.hlsli"

Texture2D<uint2> Visibility : register(t10);

RWTexture2D<float4> GBufferAlbedoMetallic : register(u0);   // Base colour, metallic
RWTexture2D<float4> GBufferNormalRoughness : register(u1);  // Shading normal, perceptual roughness
RWTexture2D<float4> GBufferGeometry : register(u2);         // Geometric normal (octahedral), ambient occlusion, front face
RWTexture2D<float4> GBufferEmissive : register(u3);         // Emissive radiance
RWTexture2D<float2> MotionOutput : register(u4);            // Current UV - previous UV
RWTexture2D<uint2> GuideOutput : register(u5);              // Denoiser guide (PackGuide)

// Barycentrics (b1, b2) of the intersection of a camera ray with a triangle's plane
float2 CameraRayBarycentrics(float2 uv, WorldTriangle t)
{
    float3 direction = CameraRayDirection(uv);
    float3 e1 = t.p1 - t.p0;
    float3 e2 = t.p2 - t.p0;
    float3 pv = cross(direction, e2);
    float det = dot(e1, pv);
    float inv_det = abs(det) > 1e-20f ? 1.0f / det : 0.0f;
    float3 tv = frame.camera_position.xyz - t.p0;
    return float2(dot(tv, pv), dot(direction, cross(tv, e1))) * inv_det;
}

// Screen motion of a point (w = 1) or a direction (w = 0, points at infinity) between last frame and this one, unjittered
float2 ScreenMotion(float4 previous_position, float4 position)
{
    float2 previous_uv = ProjectToUV(previous_position, frame.previous_view_projection_no_jitter);
    return previous_uv.x >= 0.0f ? ProjectToUV(position, frame.view_projection_no_jitter) - previous_uv : 0.0f;
}

[numthreads(8, 8, 1)]
void resolve(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (!IsOnScreen(pixel))
    {
        return;
    }
    uint2 vis = Visibility[pixel];
    float2 uv = PixelCentreUV(pixel);
    if (vis.x == 0)
    {
        // Sky: the motion of a point at infinity (camera rotation only)
        float3 direction = CameraRayDirection(uv);
        GBufferAlbedoMetallic[pixel] = 0.0f;
        GBufferNormalRoughness[pixel] = 0.0f;
        GBufferGeometry[pixel] = 0.0f;
        GBufferEmissive[pixel] = 0.0f;
        MotionOutput[pixel] = ScreenMotion(float4(direction, 0.0f), float4(direction, 0.0f));
        GuideOutput[pixel] = uint2(0, 0);
        return;
    }

    Instance instance = Instances[vis.x - 1];
    TriangleVertices tv = FetchMeshletTriangle(vis.y >> 7, vis.y & 0x7F);
    WorldTriangle triangle_world = TransformTriangle(tv, instance);
    float2 bary = CameraRayBarycentrics(uv, triangle_world);
    float2 bary_x = CameraRayBarycentrics(uv + float2(frame.inverse_resolution.x, 0.0f), triangle_world);
    float2 bary_y = CameraRayBarycentrics(uv + float2(0.0f, frame.inverse_resolution.y), triangle_world);

    SurfaceGeometry g = InterpolateSurface(tv, instance, bary, CameraRayDirection(uv));
    // Texture coordinate gradients from the barycentric differences between neighbouring pixels
    float2 uv_e1 = tv.v1.uv - tv.v0.uv;
    float2 uv_e2 = tv.v2.uv - tv.v0.uv;
    float2 ddx_uv = (bary_x.x - bary.x) * uv_e1 + (bary_x.y - bary.y) * uv_e2;
    float2 ddy_uv = (bary_y.x - bary.x) * uv_e1 + (bary_y.y - bary.y) * uv_e2;
    MaterialSample s = EvaluateMaterial(Materials[instance.material], g, LookupWithGradients(g.uv, ddx_uv, ddy_uv));

    // Motion: the surface point at its previous transform, projected with last frame's camera
    float3 b = Barycentrics(bary);
    float3 object_position = tv.v0.position * b.x + tv.v1.position * b.y + tv.v2.position * b.z;
    float4 previous_position = float4(TransformPoint(object_position, instance.previous_world), 1.0f);

    GBufferAlbedoMetallic[pixel] = float4(s.albedo, s.metallic);
    GBufferNormalRoughness[pixel] = float4(s.normal, s.roughness);
    GBufferGeometry[pixel] = float4(EncodeOctahedral(g.geometric_normal), s.ao, g.front_face ? 1.0f : 0.0f);
    GBufferEmissive[pixel] = float4(s.emissive, 0.0f);
    MotionOutput[pixel] = ScreenMotion(previous_position, float4(g.position, 1.0f));
    GuideOutput[pixel] = PackGuide(mul(float4(g.position, 1.0f), frame.view).z, s.normal, s.roughness);
}
