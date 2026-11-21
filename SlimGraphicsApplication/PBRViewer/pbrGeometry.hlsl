// GPU culling and the visibility buffer.
//
// Two phase occlusion culling: the early phase draws instances and meshlets that were visible last frame; an HZB is built
// from its depth; the late phase tests everything else against it, draws what became visible and records visibility for
// the next frame. Within a phase:
//   cull_instances (compute, one thread per instance): frustum (and late: HZB) test, appends amplification tasks
//   (PBR_TASK_MESHLETS meshlet chunks) to its render bucket's region of the task buffer
//   build_draw_arguments: one DispatchMesh argument per bucket (one indirect call per bucket)
//   as_main: per meshlet frustum, normal cone and HZB tests; dispatches surviving meshlets
//   ms_main / ps_*: rasterise instance and triangle ids into the visibility buffer
//
// Variants: PBR_SHADER_MASKED (alpha tested / double sided bucket: the mesh shader outputs texture coordinates and the pixel
// shader discards)

#include "pbrSurface.hlsli"

cbuffer GeometryConstants : register(b1)
{
    GeometryPassConstants pass;
};

RWStructuredBuffer<uint2> Tasks : register(u0);             // x = instance, y = first meshlet | drawn early flag << 31
RWStructuredBuffer<uint> TaskCounts : register(u1);         // One per bucket
RWStructuredBuffer<uint4> DrawArguments : register(u2);     // One DispatchMesh per bucket
RWStructuredBuffer<uint> InstanceVisibility : register(u3); // Last frame's late phase result per instance
RWStructuredBuffer<uint> MeshletVisibility : register(u4);  // Bit per (instance, meshlet)
RWStructuredBuffer<uint> CullStatistics : register(u5);

Texture2D<float> HZB : register(t10);                       // Late phase: farthest (minimum, reverse Z) depth pyramid

void AddStatistic(uint index, uint value)
{
    InterlockedAdd(CullStatistics[index], value);
}

bool SphereInFrustum(float4 sphere)
{
    if (!HasFlag(PBR_FLAG_FRUSTUM_CULLING))
    {
        return true;
    }
    [unroll]
    for (uint i = 0; i < 6; i++)
    {
        if (dot(float4(sphere.xyz, 1.0f), frame.frustum_planes[i]) < -sphere.w)
        {
            return false;
        }
    }
    return true;
}

// Whether a world space sphere is hidden behind the HZB (conservative: false when unsure)
bool SphereOccluded(float4 sphere)
{
    if (!HasFlag(PBR_FLAG_OCCLUSION_CULLING))
    {
        return false;
    }
    float3 c = mul(float4(sphere.xyz, 1.0f), frame.view).xyz;   // View space (left handed, +z forward)
    float r = sphere.w;
    float near_plane = frame.camera_position.w;
    if (c.z - r < near_plane)
    {
        return false; // Crosses the near plane
    }
    // Screen space bounds of the projected sphere (Mara and McGuire, "2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere")
    float p00 = frame.projection._11;
    float p11 = frame.projection._22;
    float2 cx = float2(c.x, c.z);
    float2 vx = float2(sqrt(dot(cx, cx) - r * r), r);
    float2 minx = mul(cx, float2x2(vx.x, vx.y, -vx.y, vx.x));
    float2 maxx = mul(cx, float2x2(vx.x, -vx.y, vx.y, vx.x));
    float2 cy = float2(c.y, c.z);
    float2 vy = float2(sqrt(dot(cy, cy) - r * r), r);
    float2 miny = mul(cy, float2x2(vy.x, vy.y, -vy.y, vy.x));
    float2 maxy = mul(cy, float2x2(vy.x, -vy.y, vy.y, vy.x));
    // NDC x, y -> UV (y down)
    float4 aabb = float4(minx.x / minx.y * p00, maxy.x / maxy.y * p11, maxx.x / maxx.y * p00, miny.x / miny.y * p11);
    aabb = aabb.xwzy * float4(0.5f, -0.5f, 0.5f, -0.5f) + 0.5f; // (min u, min v, max u, max v)
    aabb = saturate(aabb);
    float2 size = (aabb.zw - aabb.xy) * frame.hzb_size;
    float level = ceil(log2(max(max(size.x, size.y), 1.0f)));
    level = min(level, (float)frame.hzb_mip_count - 1.0f);
    // The four texels covering the bounds at that level hold the farthest depth around it
    float2 texel = exp2(level) / frame.hzb_size;
    float d0 = HZB.SampleLevel(PointClampSampler, aabb.xy, level);
    float d1 = HZB.SampleLevel(PointClampSampler, float2(aabb.z, aabb.y), level);
    float d2 = HZB.SampleLevel(PointClampSampler, float2(aabb.x, aabb.w), level);
    float d3 = HZB.SampleLevel(PointClampSampler, aabb.zw, level);
    float farthest = min(min(d0, d1), min(d2, d3));
    // Nearest point of the sphere, reverse Z infinite projection: depth = near / z
    float sphere_depth = near_plane / (c.z - r);
    return sphere_depth < farthest;
}

float4 TransformSphere(float4 sphere, float4x4 world)
{
    float3 centre = mul(float4(sphere.xyz, 1.0f), world).xyz;
    float scale = sqrt(max(max(dot(world[0].xyz, world[0].xyz), dot(world[1].xyz, world[1].xyz)), dot(world[2].xyz, world[2].xyz)));
    return float4(centre, sphere.w * scale);
}

// ---------------------------------------------------------------------------------------------------------------------

[numthreads(64, 1, 1)]
void cull_instances(uint3 dtid : SV_DispatchThreadID)
{
    uint instance_index = dtid.x;
    if (instance_index >= pass.instance_count)
    {
        return;
    }
    Instance instance = Instances[instance_index];
    bool occlusion = HasFlag(PBR_FLAG_OCCLUSION_CULLING);
    bool in_frustum = SphereInFrustum(instance.world_sphere);
    bool visible_last_frame = !occlusion || InstanceVisibility[instance_index] != 0;

    bool emit = false;
    bool drawn_early = false;
    if (pass.phase == PBR_PHASE_EARLY)
    {
        // Without occlusion culling everything in the frustum is drawn here and the late phase draws nothing
        emit = in_frustum && visible_last_frame;
        drawn_early = true;
        if (!in_frustum)
        {
            AddStatistic(PBR_STAT_INSTANCES_FRUSTUM_CULLED, 1);
        }
    }
    else
    {
        if (!occlusion)
        {
            return;
        }
        bool visible = in_frustum && !SphereOccluded(instance.world_sphere);
        if (in_frustum && !visible)
        {
            AddStatistic(PBR_STAT_INSTANCES_OCCLUSION_CULLED, 1);
        }
        InstanceVisibility[instance_index] = visible ? 1u : 0u;
        // Instances drawn early still go through the late phase: their meshlets that were hidden last frame may show now
        emit = visible;
        drawn_early = visible_last_frame && in_frustum;
    }
    if (!emit)
    {
        return;
    }

    Mesh mesh = Meshes[instance.mesh];
    uint bucket = instance.flags & PBR_INSTANCE_BUCKET_MASK;
    uint task_count = (mesh.meshlet_count + PBR_TASK_MESHLETS - 1) / PBR_TASK_MESHLETS;
    uint first;
    InterlockedAdd(TaskCounts[bucket], task_count, first);
    uint capacity = pass.task_region_sizes[bucket];
    for (uint i = 0; i < task_count && first + i < capacity; i++)
    {
        Tasks[pass.task_region_offsets[bucket] + first + i] = uint2(instance_index, (i * PBR_TASK_MESHLETS) | (drawn_early ? 0x80000000u : 0u));
    }
}

[numthreads(PBR_BUCKET_COUNT, 1, 1)]
void build_draw_arguments(uint3 dtid : SV_DispatchThreadID)
{
    uint bucket = dtid.x;
    uint count = min(TaskCounts[bucket], pass.task_region_sizes[bucket]);
    TaskCounts[bucket] = count;
    DrawArguments[bucket] = uint4(min(count, PBR_MAX_DISPATCH), (count + PBR_MAX_DISPATCH - 1) / PBR_MAX_DISPATCH, 1, 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Amplification: one meshlet per thread

struct Payload
{
    uint instance;
    uint meshlets[PBR_TASK_MESHLETS];
};

groupshared Payload s_payload;
groupshared uint s_visible_count;

[numthreads(PBR_TASK_MESHLETS, 1, 1)]
void as_main(uint gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    if (gtid == 0)
    {
        s_visible_count = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint task_index = gid.y * PBR_MAX_DISPATCH + gid.x;
    bool valid = task_index < TaskCounts[pass.bucket];
    uint2 task = valid ? Tasks[pass.task_region_offsets[pass.bucket] + task_index] : uint2(0, 0);
    uint instance_index = task.x;
    bool instance_drawn_early = (task.y & 0x80000000u) != 0;
    uint local = (task.y & 0x7FFFFFFFu) + gtid;

    Instance instance = Instances[instance_index];
    Mesh mesh = Meshes[instance.mesh];
    bool visible = valid && local < mesh.meshlet_count;
    uint meshlet_index = mesh.meshlet_offset + local;
    uint bit = instance.meshlet_visibility_offset + local;
    bool occlusion = HasFlag(PBR_FLAG_OCCLUSION_CULLING);

    if (visible)
    {
        Meshlet meshlet = Meshlets[meshlet_index];
        bool was_visible = !occlusion || (MeshletVisibility[bit >> 5] & (1u << (bit & 31))) != 0;
        if (pass.phase == PBR_PHASE_EARLY && !was_visible)
        {
            visible = false;
        }
        else
        {
            float4 sphere = TransformSphere(meshlet.sphere, instance.world);
            bool culled = false;
            if (!SphereInFrustum(sphere))
            {
                culled = true;
                AddStatistic(PBR_STAT_MESHLETS_FRUSTUM_CULLED, 1);
            }
            // Normal cone (only for single sided geometry)
            if (!culled && HasFlag(PBR_FLAG_CONE_CULLING) && (instance.flags & PBR_INSTANCE_BUCKET_MASK) == PBR_BUCKET_OPAQUE && meshlet.cone_apex.w < 1.0f)
            {
                float3 apex = mul(float4(meshlet.cone_apex.xyz, 1.0f), instance.world).xyz;
                float3 axis = normalize(mul(meshlet.cone_axis.xyz, (float3x3)instance.world));
                if ((instance.flags & PBR_INSTANCE_MIRRORED) != 0)
                {
                    axis = -axis;
                }
                if (dot(normalize(apex - frame.culling_camera_position.xyz), axis) >= meshlet.cone_apex.w)
                {
                    culled = true;
                    AddStatistic(PBR_STAT_MESHLETS_CONE_CULLED, 1);
                }
            }
            if (pass.phase == PBR_PHASE_LATE)
            {
                bool occluded = !culled && SphereOccluded(sphere);
                if (occluded)
                {
                    AddStatistic(PBR_STAT_MESHLETS_OCCLUSION_CULLED, 1);
                }
                bool now_visible = !culled && !occluded;
                // Visibility for the next frame's early phase
                if (now_visible)
                {
                    InterlockedOr(MeshletVisibility[bit >> 5], 1u << (bit & 31));
                }
                else
                {
                    InterlockedAnd(MeshletVisibility[bit >> 5], ~(1u << (bit & 31)));
                }
                // Drawn already by the early phase
                visible = now_visible && !(instance_drawn_early && was_visible);
            }
            else
            {
                visible = !culled;
            }
        }
    }

    if (visible)
    {
        uint slot;
        InterlockedAdd(s_visible_count, 1, slot);
        s_payload.meshlets[slot] = meshlet_index;
        AddStatistic(PBR_STAT_TRIANGLES_DRAWN, Meshlets[meshlet_index].triangle_count);
    }
    if (gtid == 0)
    {
        s_payload.instance = instance_index;
    }
    GroupMemoryBarrierWithGroupSync();
    if (gtid == 0 && s_visible_count > 0)
    {
        AddStatistic(pass.phase == PBR_PHASE_EARLY ? PBR_STAT_MESHLETS_DRAWN_EARLY : PBR_STAT_MESHLETS_DRAWN_LATE, s_visible_count);
    }
    DispatchMesh(s_visible_count, 1, 1, s_payload);
}

// ---------------------------------------------------------------------------------------------------------------------
// Mesh shader: one meshlet per group

struct VertexOutput
{
    float4 position : SV_Position;
#ifdef PBR_SHADER_MASKED
    float2 uv : TEXCOORD0;
#endif
};

struct PrimitiveOutput
{
    nointerpolation uint2 visibility : VISIBILITY;
#ifdef PBR_SHADER_MASKED
    nointerpolation uint material : MATERIAL;
#endif
};

[numthreads(128, 1, 1)]
[outputtopology("triangle")]
void ms_main(uint gtid : SV_GroupThreadID, uint3 gid : SV_GroupID, in payload Payload payload,
    out vertices VertexOutput verts[PBR_MESHLET_MAX_VERTICES], out indices uint3 tris[PBR_MESHLET_MAX_TRIANGLES], out primitives PrimitiveOutput prims[PBR_MESHLET_MAX_TRIANGLES])
{
    uint meshlet_index = payload.meshlets[gid.x];
    Meshlet meshlet = Meshlets[meshlet_index];
    Instance instance = Instances[payload.instance];
    SetMeshOutputCounts(meshlet.vertex_count, meshlet.triangle_count);

    if (gtid < meshlet.vertex_count)
    {
        Vertex v = Vertices[MeshletVertices[meshlet.vertex_offset + gtid]];
        float4 world = mul(float4(v.position, 1.0f), instance.world);
        VertexOutput o;
        o.position = mul(world, frame.view_projection);
#ifdef PBR_SHADER_MASKED
        o.uv = v.uv;
#endif
        verts[gtid] = o;
    }
    if (gtid < meshlet.triangle_count)
    {
        uint packed = MeshletTriangles[meshlet.triangle_offset + gtid];
        uint3 t = uint3(packed & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF);
        // Mirrored instances reverse the winding; restore it so back face culling keeps the right side
        tris[gtid] = (instance.flags & PBR_INSTANCE_MIRRORED) ? t.xzy : t;
        PrimitiveOutput p;
        p.visibility = PackVisibility(payload.instance, meshlet_index, gtid);
#ifdef PBR_SHADER_MASKED
        p.material = instance.material;
#endif
        prims[gtid] = p;
    }
}

uint2 ps_main(VertexOutput input, PrimitiveOutput primitive) : SV_Target
{
#ifdef PBR_SHADER_MASKED
    Material material = Materials[primitive.material];
    TextureLookup lookup = LookupWithGradients(input.uv, ddx(input.uv), ddy(input.uv));
    if (!PassesAlphaTest(material, lookup))
    {
        discard;
    }
#endif
    return primitive.visibility;
}

// ---------------------------------------------------------------------------------------------------------------------
// HZB: farthest depth (minimum, reverse Z) pyramid. Level 0 covers the depth buffer with a power of two size; each texel
// takes the minimum over the depth pixels its footprint touches.

Texture2D<float> DepthBuffer : register(t11);
RWTexture2D<float> HZBSource : register(u6);
RWTexture2D<float> HZBDestination : register(u7);

[numthreads(8, 8, 1)]
void build_hzb_first(uint3 dtid : SV_DispatchThreadID)
{
    uint2 size = (uint2)frame.hzb_size;
    if (any(dtid.xy >= size))
    {
        return;
    }
    float2 scale = frame.resolution / frame.hzb_size;
    int2 lo = (int2)floor(dtid.xy * scale);
    int2 hi = min((int2)ceil((dtid.xy + 1) * scale), (int2)frame.resolution) - 1;
    float d = 1.0f;
    for (int y = lo.y; y <= hi.y; y++)
    {
        for (int x = lo.x; x <= hi.x; x++)
        {
            d = min(d, DepthBuffer.Load(int3(x, y, 0)));
        }
    }
    HZBDestination[dtid.xy] = d;
}

[numthreads(8, 8, 1)]
void build_hzb_down(uint3 dtid : SV_DispatchThreadID)
{
    uint2 size = max((uint2)frame.hzb_size >> pass.phase, 1u); // 'phase' holds the destination level
    if (any(dtid.xy >= size))
    {
        return;
    }
    uint2 source_size = max((uint2)frame.hzb_size >> (pass.phase - 1), 1u);
    uint2 s = min(dtid.xy * 2, source_size - 1);
    uint2 s1 = min(dtid.xy * 2 + 1, source_size - 1);
    float d = min(min(HZBSource[s], HZBSource[uint2(s1.x, s.y)]), min(HZBSource[uint2(s.x, s1.y)], HZBSource[s1]));
    HZBDestination[dtid.xy] = d;
}
