#include "gdrShaderStructures.h"

// Shader variants (GDRShaders.slimshaderjson). Each render bucket (GDR_BUCKET_*) is drawn by its own indirect DispatchMesh
// with shaders specialised for it, so the mesh and pixel shaders never branch on the type of meshlet or material:
//  - no define:          opaque meshes (GDR_BUCKET_OPAQUE)
//  - GDR_SHADER_FOLIAGE: alpha tested and double sided meshes (GDR_BUCKET_FOLIAGE)
//  - GDR_SHADER_TERRAIN: terrain tiles (GDR_BUCKET_TERRAIN)

struct Vertex
{
    float3 position;
    float3 colour;
    float2 uvs;
    float3 normals;
    float3 tangents;
};

// Read Buffers
StructuredBuffer<Vertex> Vertices : register(t0);
StructuredBuffer<MeshletData> Meshlets : register(t1);
StructuredBuffer<uint> UniqueVertexIndices : register(t2);
StructuredBuffer<uint> PrimitiveIndices : register(t3);
StructuredBuffer<InstanceData> Instances : register(t4);
// Shading (mesh and pixel shaders)
StructuredBuffer<MaterialData> Materials : register(t5);
StructuredBuffer<LightData> Lights : register(t6);
StructuredBuffer<EnvironmentData> Environment : register(t7);
StructuredBuffer<TerrainData> Terrains : register(t8);
StructuredBuffer<float> TerrainHeights : register(t9);
Texture2DArray<float4> MaterialTextures : register(t10);
SamplerState MaterialSampler : register(s0);    // Anisotropic
SamplerState TrilinearSampler : register(s3);   // Alpha testing

// Write Buffers
RWStructuredBuffer<PerInstanceProcessingData_Output> PerInstanceDataOutput : register(u0);
RWStructuredBuffer<uint4> AtomicCounter : register(u1);
RWStructuredBuffer<uint4> IndirectArgumentBuffer : register(u2);

// Feedback Support
#ifdef GDR_DEBUG
RWStructuredBuffer<DebugFeedbackData> DebugFeedbackBuffer : register(u3);
#endif

namespace Feedback
{
    void IncrementMeshletCullTotal()
    {       
        #ifdef GDR_DEBUG
            uint original;
            InterlockedAdd(DebugFeedbackBuffer[0].meshlets_culled_total, 1, original);
        #endif
    }
    void IncrementMeshletCullFrustum()
    {       
        #ifdef GDR_DEBUG
            uint original;
            InterlockedAdd(DebugFeedbackBuffer[0].meshlets_cullled_by_frustum, 1, original);
        #endif
    }
    void IncrementMeshletCullCone()
    {       
        #ifdef GDR_DEBUG
            uint original;
            InterlockedAdd(DebugFeedbackBuffer[0].meshlets_culled_by_cone, 1, original);
        #endif
    }
}


cbuffer ConstantBufferData : register(b0)
{
    CameraData camera;
};

cbuffer PerInstanceProcessingDataCBuffer : register(b1)
{
    PerInstanceProcessingData constant_data;
};

#ifdef GDR_DEBUG
cbuffer DebugConstantBufferData : register(b2)
{
    DebugData debug;
};

cbuffer CullingCameraConstantBufferData : register(b3)
{
    CameraData culling_camera;
};
#endif

DebugRenderMode GetDebugRenderMode()
{
#ifdef GDR_DEBUG
    return debug.debug_render_mode;
#else
    return DebugRenderMode::None;
#endif
}

CameraData GetCameraData()
{
    return camera;

}
CameraData GetCullingCameraData() // We separate the render camera and the culling camera so we can freeze the culling camera to inspect the scene.
{
#ifdef GDR_DEBUG
    return culling_camera;
#else
    return camera;
#endif
}

bool DoFrustumCulling()
{
#ifdef GDR_DEBUG
    return debug.use_frustum_culling;
#else
    return true;
#endif
}

bool DoConeCulling()
{
#ifdef GDR_DEBUG
    return debug.use_cone_culling;
#else
    return true;
#endif
}

bool CullAllInstances()
{
#ifdef GDR_DEBUG
    return debug.cull_all_instances != 0;
#else
    return false;
#endif
}

// Largest axis scale of a model matrix = longest basis row. (The diagonal terms are wrong once the matrix contains rotation.)
float GetMaxScale(row_major float4x4 m)
{
    return sqrt(max(max(dot(m[0].xyz, m[0].xyz), dot(m[1].xyz, m[1].xyz)), dot(m[2].xyz, m[2].xyz)));
}

// DispatchMesh is limited to 65535 groups per dimension, so large meshlet counts are spread over X and Y.
#define GDR_MAX_DISPATCH_X 65535u

float CalculateSortKey(MeshletData meshlet, InstanceData instance)
{
    float4 sphere_data = mul(float4(meshlet.spherepos_xyz_radius_w.xyz, 1.0), instance.model_matrix);
    float model_matrix_highest_scale = GetMaxScale(instance.model_matrix);
    sphere_data.w = meshlet.spherepos_xyz_radius_w.w * model_matrix_highest_scale;
    
    float3 camera_to_sphere_centre = GetCullingCameraData().camera_position.xyz - sphere_data.xyz;
    float distance = length(camera_to_sphere_centre);
    float distance_to_edge = distance - sphere_data.w;
    
    return max(0.0f, distance_to_edge);
}

// Computes visiblity of an instance
// Performs a simple world-space bounding sphere vs. frustum plane check.
bool IsMeshletSphereInFrustum(MeshletData meshlet, InstanceData instance)
{
    float4 sphere_data = mul(float4(meshlet.spherepos_xyz_radius_w.xyz, 1.0), instance.model_matrix);
    float model_matrix_highest_scale = GetMaxScale(instance.model_matrix);
    sphere_data.w = meshlet.spherepos_xyz_radius_w.w * model_matrix_highest_scale;

    CameraData camera_data = GetCullingCameraData();
    float4 center = float4(sphere_data.xyz, 1.0);
    float radius = sphere_data.w;

    for (int i = 0; i < 6; ++i)
    {
        if (dot(center, camera_data.Planes[i]) < -radius)
        {
            return false;
        }
    }

    return true;
}

bool IsMeshletVisibleNormalCone(MeshletData meshlet, InstanceData instance)
{
    float3 cone_apex = mul(float4(meshlet.cone_apex_xyz.xyz, 1.0f), instance.model_matrix).xyz;
    float3 cone_axis = normalize(mul(meshlet.cone_axis_xyz_cone_cutoff_w.xyz, (float3x3) instance.model_matrix));
    
    if (dot(normalize(cone_apex - GetCullingCameraData().camera_position.xyz), cone_axis) >= meshlet.cone_axis_xyz_cone_cutoff_w.w)
    {    
        return false;
    }
    return true;
}

[numthreads(32, 1, 1)]
void per_instance_processing_main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint thread_id = dispatch_id.x;
    
    if (thread_id < constant_data.meshlets_to_process_count)
    {
        uint instance_index = constant_data.instance_offset + (thread_id / constant_data.meshlets_per_instance);
        uint meshlet_index = constant_data.meshlet_offset + (thread_id % constant_data.meshlets_per_instance);
        
        InstanceData instance = Instances[instance_index];
        MeshletData meshlet = Meshlets[meshlet_index];
        
        bool visible = true;
        
        if (visible && CullAllInstances())
        {
            visible = false;
        }
        
        // Not for double sided buckets: their back faces are visible
        if (visible && constant_data.cone_culling != 0 && DoConeCulling())
        {
            visible = IsMeshletVisibleNormalCone(meshlet, instance);         
            if (!visible) Feedback::IncrementMeshletCullCone();
        }
        
        if (visible && DoFrustumCulling())
        {            
            visible = IsMeshletSphereInFrustum(meshlet, instance);
            if (!visible) Feedback::IncrementMeshletCullFrustum();
        }             
        
        if (visible) // If we're visible, add to the output list of meshests to be rendered
        {
            // Appended to the region of the visible meshlet list that belongs to this section's bucket (one counter per bucket)
            uint data_output_index;
            InterlockedAdd(AtomicCounter[constant_data.bucket].x, 1, data_output_index);
            
            PerInstanceProcessingData_Output meshlet_out;
            meshlet_out.instance_index = instance_index;
            meshlet_out.meshlet_index = meshlet_index;
            meshlet_out.sort_key = CalculateSortKey(meshlet, instance);
            meshlet_out.pad = 0;
            
            PerInstanceDataOutput[constant_data.bucket_output_offset + data_output_index] = meshlet_out;
        }
        else
        {
            Feedback::IncrementMeshletCullTotal();
        }
    }
}

// One thread per render bucket: the DispatchMesh arguments of each bucket's indirect call
[numthreads(GDR_BUCKET_COUNT, 1, 1)]
void build_indirect_arguments(uint3 dtid : SV_DispatchThreadID)
{
    // One mesh shader group per visible meshlet, split over X/Y to respect the 65535 per-dimension limit.
    // ms_main reconstructs the linear index as gid.y * GDR_MAX_DISPATCH_X + gid.x.
    uint bucket = dtid.x;
    uint count = AtomicCounter[bucket].x;
    uint x = min(count, GDR_MAX_DISPATCH_X);
    uint y = (count + GDR_MAX_DISPATCH_X - 1) / GDR_MAX_DISPATCH_X; // 0 when nothing is visible
    IndirectArgumentBuffer[bucket] = uint4(x, y, 1, 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Mesh shading: every visible meshlet is one mesh shader group of its bucket's indirect dispatch. constant_data holds the
// bucket and the start of its region of the visible meshlet list.

struct PSInput
{
    float4 position : SV_POSITION;
    float3 colour : COLOR0;                         // Instance (and vertex) colour, gamma space
    float2 uvs : TEXCOORD0;                         // Terrain: model space x / z
    float3 normal : NORMAL;                         // World space
    float3 tangent : TANGENT;                       // World space (zero when the mesh has none)
    float3 world_position : TEXCOORD1;
    float terrain_height : TEXCOORD2;               // Terrain height before water flattening (water depth)
    nointerpolation uint material_index : TEXCOORD3;
    nointerpolation float sort_position : TEXCOORD4; // 0 = first meshlet drawn, 1 = last (DebugRenderMode::SortOrder)
};

// Transforms normals by the inverse transpose of a model matrix's 3x3 part (its cofactor matrix, up to scale), so they stay
// perpendicular to surfaces under non uniform scale
float3x3 NormalMatrix(row_major float4x4 m)
{
    float3 r0 = m[0].xyz, r1 = m[1].xyz, r2 = m[2].xyz;
    float3x3 cofactor = float3x3(cross(r1, r2), cross(r2, r0), cross(r0, r1));
    return dot(r0, cross(r1, r2)) < 0.0f ? -cofactor : cofactor; // Mirroring transforms flip the winding, keep normals outward
}

float TerrainHeight(TerrainData terrain, int x, int z)
{
    x = clamp(x, 0, (int)terrain.quads);
    z = clamp(z, 0, (int)terrain.quads);
    return TerrainHeights[terrain.height_offset + (uint)z * (terrain.quads + 1u) + (uint)x];
}

// Vertex indices of a terrain tile triangle; must match gdr::terrain_tile_triangle (gdrTerrain.h)
uint3 TerrainTileTriangle(uint triangle_index)
{
    uint quad = triangle_index / 2u;
    uint qx = quad % GDR_TERRAIN_TILE_QUADS;
    uint qz = quad / GDR_TERRAIN_TILE_QUADS;
    uint row = GDR_TERRAIN_TILE_QUADS + 1u;
    uint v00 = qz * row + qx;
    uint v10 = v00 + 1u;
    uint v01 = v00 + row;
    uint v11 = v01 + 1u;
    bool second = (triangle_index & 1u) != 0u;
    if (((qx + qz) & 1u) == 0u)
    {
        return second ? uint3(v10, v01, v11) : uint3(v00, v01, v10);
    }
    return second ? uint3(v00, v11, v10) : uint3(v00, v01, v11);
}

[numthreads(128, 1, 1)]
[OutputTopology("triangle")]
void ms_main(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID, out indices uint3 tris[124], out vertices PSInput verts[64])
{
    uint meshlet_list_index = gid.y * GDR_MAX_DISPATCH_X + gid.x;
    uint visible_count = AtomicCounter[constant_data.bucket].x;
    bool valid_thread = meshlet_list_index < visible_count;
    uint out_vertex_count = 0;
    uint out_triangle_count = 0;

    PerInstanceProcessingData_Output instance_stage_output;
    MeshletData meshlet;
    InstanceData instance;

    if (valid_thread)
    {
        instance_stage_output = PerInstanceDataOutput[constant_data.bucket_output_offset + meshlet_list_index];
        meshlet = Meshlets[instance_stage_output.meshlet_index];
        instance = Instances[instance_stage_output.instance_index];

        out_vertex_count = meshlet.vertex_count;
        out_triangle_count = meshlet.triangle_count;
    }

    SetMeshOutputCounts(out_vertex_count, out_triangle_count);

    if (valid_thread)
    {
        if (gtid.x < meshlet.vertex_count)
        {
            float3 position;
            float3 normal;
            float3 tangent;
            float3 colour;
            float2 uvs;
            float terrain_height = 0.0f;
#ifdef GDR_SHADER_TERRAIN
            {
                // Grid vertex of the tile: position from the height grid, normal from the neighbouring heights
                TerrainData t = Terrains[meshlet.terrain_index];
                int gx = (int)(meshlet.terrain_tile_x + gtid.x % (GDR_TERRAIN_TILE_QUADS + 1u));
                int gz = (int)(meshlet.terrain_tile_z + gtid.x / (GDR_TERRAIN_TILE_QUADS + 1u));
                terrain_height = TerrainHeight(t, gx, gz);
                float slope_x = TerrainHeight(t, gx - 1, gz) - TerrainHeight(t, gx + 1, gz);
                float slope_z = TerrainHeight(t, gx, gz - 1) - TerrainHeight(t, gx, gz + 1);
                bool water = terrain_height < t.water_level;
                position = float3(t.origin_x + gx * t.spacing, max(terrain_height, t.water_level), t.origin_z + gz * t.spacing);
                normal = water ? float3(0, 1, 0) : float3(slope_x, 2.0f * t.spacing, slope_z);
                tangent = float3(1, 0, 0);
                colour = float3(1, 1, 1);
                uvs = position.xz;
            }
#else
            {
                uint vertex_index = UniqueVertexIndices[meshlet.vertex_offset + gtid.x];
                Vertex v = Vertices[meshlet.vertex_buffer_offset + vertex_index];
                position = v.position;
                normal = v.normals;
                tangent = v.tangents;
                colour = v.colour;
                uvs = v.uvs;
            }
#endif

            float4 world_position = mul(float4(position, 1.0f), instance.model_matrix);
            float3 world_normal = mul(normal, NormalMatrix(instance.model_matrix));
            float3 world_tangent = mul(tangent, (float3x3) instance.model_matrix);

            PSInput vertex_output;
            vertex_output.position = mul(world_position, GetCameraData().view_projection_matrix);
            vertex_output.world_position = world_position.xyz;
            vertex_output.colour = colour * instance.colour.xyz;
            vertex_output.uvs = uvs;
            vertex_output.normal = dot(world_normal, world_normal) > 0.0f ? normalize(world_normal) : float3(0, 1, 0);
            vertex_output.tangent = dot(world_tangent, world_tangent) > 0.0f ? normalize(world_tangent) : float3(0, 0, 0);
            vertex_output.terrain_height = terrain_height;
            vertex_output.material_index = meshlet.material_index;
            vertex_output.sort_position = (float) meshlet_list_index / (float) max(visible_count, 1u);
            verts[gtid.x] = vertex_output;
        }

        if (gtid.x < meshlet.triangle_count)
        {
#ifdef GDR_SHADER_TERRAIN
            tris[gtid.x] = TerrainTileTriangle(gtid.x);
#else
            uint packedIndices = PrimitiveIndices[meshlet.triangle_offset + gtid.x];
            tris[gtid.x] = uint3(packedIndices & 0xFF, (packedIndices >> 8) & 0xFF, (packedIndices >> 16) & 0xFF);
#endif
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Pixel shading

float3 GammaToLinear(float3 c)
{
    return pow(max(c, 0.0f), 2.2f);
}

// Filmic tone curve (Narkowicz's ACES fit), then gamma encoding for the UNORM render target
float3 LinearToDisplay(float3 c)
{
    c = saturate((c * (2.51f * c + 0.03f)) / (c * (2.43f * c + 0.59f) + 0.14f));
    return pow(c, 1.0f / 2.2f);
}

// Integer hashed value noise (exact on every backend), 0..1
float Hash(int2 p)
{
    uint2 q = asuint(p) * uint2(1597334673u, 3812015801u);
    uint n = (q.x ^ q.y) * 1597334673u;
    n ^= n >> 16;
    return (float)(n & 0xFFFFu) / 65535.0f;
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = p - i;
    f = f * f * (3.0f - 2.0f * f);
    int2 c = (int2) i;
    float a = Hash(c), b = Hash(c + int2(1, 0)), d = Hash(c + int2(0, 1)), e = Hash(c + int2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(d, e, f.x), f.y);
}

float FractalNoise(float2 p)
{
    return ValueNoise(p) * 0.5f + ValueNoise(p * 2.03f + 17.1f) * 0.3f + ValueNoise(p * 4.11f + 31.7f) * 0.2f;
}

struct Surface
{
    float3 albedo;              // Linear
    float3 normal;              // World space
    float3 specular_colour;
    float specular_power;
};

// Procedural terrain materials: grass tones, rock on steep slopes, sand along the shore, snow on high flat ground and
// water whose colour deepens with depth. Noise in model space keeps the patterns fixed to the terrain.
Surface ShadeTerrain(TerrainData t, PSInput input, float3 normal)
{
    float2 p = input.uvs;
    float height = input.terrain_height;
    float slope = 1.0f - saturate(normal.y);
    float broad = FractalNoise(p * 0.035f);
    float detail = FractalNoise(p * 0.6f);
    float fine = ValueNoise(p * 4.0f);

    float3 grass = GammaToLinear(t.grass_colour.rgb);
    float3 dry = GammaToLinear(t.grass_colour.rgb * float3(1.35f, 1.2f, 0.7f));
    float3 colour = lerp(grass, dry, smoothstep(0.35f, 0.75f, broad)) * (0.8f + 0.35f * detail) * (0.92f + 0.16f * fine);

    float3 sand = GammaToLinear(t.sand_colour.rgb) * (0.9f + 0.2f * detail);
    float shore = t.water_level > -1.0e30f ? 1.0f - smoothstep(0.3f, 1.2f + detail, height - t.water_level) : 0.0f;
    colour = lerp(colour, sand, shore);

    float3 rock = GammaToLinear(t.rock_colour.rgb) * (0.7f + 0.5f * detail) * (0.9f + 0.2f * fine);
    float rockiness = smoothstep(0.28f, 0.45f, slope + (detail - 0.5f) * 0.2f);
    colour = lerp(colour, rock, rockiness);

    float snow = smoothstep(t.snow_height, t.snow_height + 2.0f, height + (broad - 0.5f) * 6.0f) * (1.0f - smoothstep(0.45f, 0.6f, slope));
    colour = lerp(colour, GammaToLinear(t.snow_colour.rgb), snow);

    Surface s;
    s.albedo = colour * GammaToLinear(input.colour);
    s.normal = normal;
    s.specular_colour = lerp(float3(0.02f, 0.02f, 0.02f), float3(0.25f, 0.25f, 0.25f), snow);
    s.specular_power = lerp(12.0f, 48.0f, snow);

    if (height < t.water_level)
    {
        float depth = t.water_level - height;
        float3 shallow = GammaToLinear(t.water_colour.rgb * float3(0.8f, 1.5f, 1.3f));
        float3 deep = GammaToLinear(t.water_colour.rgb * 0.45f);
        s.albedo = lerp(shallow, deep, saturate(depth * 0.35f));
        // Static ripples tilt the normal a little so the sun's highlight breaks up
        float2 ripple = float2(ValueNoise(p * 1.7f), ValueNoise(p * 1.7f + 9.3f)) - 0.5f;
        s.normal = normalize(float3(ripple.x * 0.12f, 1.0f, ripple.y * 0.12f));
        s.specular_colour = float3(0.6f, 0.6f, 0.6f);
        s.specular_power = 160.0f;
    }
    return s;
}

float3 ApplyLighting(Surface s, float3 world_position)
{
    EnvironmentData environment = Environment[0];
    float3 view = normalize(GetCameraData().camera_position.xyz - world_position);
    float3 n = s.normal;

    float3 colour = lerp(environment.ground_colour.rgb, environment.sky_colour.rgb, n.y * 0.5f + 0.5f) * s.albedo;
    float specular_normalisation = (s.specular_power + 8.0f) / (8.0f * 3.14159265f);
    for (uint i = 0; i < environment.light_count; i++)
    {
        LightData light = Lights[i];
        float3 to_light;
        float attenuation = 1.0f;
        if (light.type == GDR_LIGHT_DIRECTIONAL)
        {
            to_light = -light.direction.xyz;
        }
        else
        {
            float3 offset = light.position_range.xyz - world_position;
            float distance_squared = dot(offset, offset);
            float range = light.position_range.w;
            if (distance_squared >= range * range)
            {
                continue;
            }
            // Inverse square falloff, windowed to reach zero at the light's range
            float distance = sqrt(distance_squared);
            to_light = offset / max(distance, 1e-4f);
            float ratio = distance / range;
            float window = saturate(1.0f - ratio * ratio * ratio * ratio);
            attenuation = window * window / (distance_squared + 1.0f);
        }
        float n_dot_l = saturate(dot(n, to_light));
        if (n_dot_l <= 0.0f)
        {
            continue;
        }
        float3 half_vector = normalize(to_light + view);
        float specular = pow(saturate(dot(n, half_vector)), s.specular_power) * specular_normalisation;
        colour += light.colour.rgb * attenuation * n_dot_l * (s.albedo + s.specular_colour * specular);
    }

    float fog_distance = max(length(GetCameraData().camera_position.xyz - world_position) - environment.fog_start, 0.0f);
    float fog = 1.0f - exp(-environment.fog_colour.a * fog_distance);
    colour = lerp(colour, environment.fog_colour.rgb, fog);
    return colour * environment.exposure;
}

float4 ps_main(PSInput input, bool is_front_face : SV_IsFrontFace) : SV_TARGET
{
    MaterialData material = Materials[input.material_index];
    // Texture coordinate derivatives taken outside the material branches, which may not be uniform across a pixel quad
    float2 duv_dx = ddx(input.uvs);
    float2 duv_dy = ddy(input.uvs);

    float3 normal = normalize(input.normal);
    Surface s;
#ifdef GDR_SHADER_TERRAIN
    s = ShadeTerrain(Terrains[material.terrain_index], input, normal);
#else
    {
        s.albedo = GammaToLinear(input.colour * material.diffuse_colour.rgb);
        s.normal = normal;
        s.specular_colour = material.specular.rgb;
        s.specular_power = material.specular.a;

        if (material.texture_diffuse_index != GDR_NO_TEXTURE)
        {
            float4 diffuse = MaterialTextures.SampleGrad(MaterialSampler, float3(input.uvs, (float) material.texture_diffuse_index), duv_dx, duv_dy);
            s.albedo *= GammaToLinear(diffuse.rgb);
        }
#ifdef GDR_SHADER_FOLIAGE
        // Only the foliage bucket uses this variant: discard stops drivers rejecting hidden pixels before shading, which
        // costs performance (and makes pixel shader counts depend on GPU timing).
        // Opacity is read at an explicit mip level with a trilinear sampler: anisotropic filtering of an explicit level is
        // implementation defined (D3D12 and Vulkan drivers cut out different edge pixels), and this also keeps cut outs stable
        // as the view moves.
        if ((material.flags & GDR_MATERIAL_ALPHA_TEST) != 0u)
        {
            float width, height, slices;
            MaterialTextures.GetDimensions(width, height, slices);
            float2 texels_dx = duv_dx * width;
            float2 texels_dy = duv_dy * height;
            float lod = max(0.5f * log2(max(max(dot(texels_dx, texels_dx), dot(texels_dy, texels_dy)), 1e-8f)), 0.0f);
            float opacity = material.texture_opacity_index != GDR_NO_TEXTURE ?
                MaterialTextures.SampleLevel(TrilinearSampler, float3(input.uvs, (float) material.texture_opacity_index), lod).r :
                MaterialTextures.SampleLevel(TrilinearSampler, float3(input.uvs, (float) material.texture_diffuse_index), lod).a;
            if (opacity < 0.5f)
            {
                discard;
            }
        }
#endif
        if (material.texture_specular_index != GDR_NO_TEXTURE)
        {
            s.specular_colour *= MaterialTextures.SampleGrad(MaterialSampler, float3(input.uvs, (float) material.texture_specular_index), duv_dx, duv_dy).rgb;
        }

        // Normal and bump maps need a tangent frame
        float3 tangent = input.tangent - normal * dot(normal, input.tangent);
        if (material.texture_normals_index != GDR_NO_TEXTURE && dot(tangent, tangent) > 1e-8f)
        {
            tangent = normalize(tangent);
            float3 bitangent = cross(normal, tangent);
            float slice = (float) material.texture_normals_index;
            if ((material.flags & GDR_MATERIAL_BUMP_MAP) != 0u)
            {
                // Height map: slope from neighbouring texels
                float width, height, slices;
                MaterialTextures.GetDimensions(width, height, slices);
                float2 texel = 1.0f / float2(width, height);
                float h_left = MaterialTextures.SampleGrad(MaterialSampler, float3(input.uvs - float2(texel.x, 0), slice), duv_dx, duv_dy).r;
                float h_right = MaterialTextures.SampleGrad(MaterialSampler, float3(input.uvs + float2(texel.x, 0), slice), duv_dx, duv_dy).r;
                float h_down = MaterialTextures.SampleGrad(MaterialSampler, float3(input.uvs - float2(0, texel.y), slice), duv_dx, duv_dy).r;
                float h_up = MaterialTextures.SampleGrad(MaterialSampler, float3(input.uvs + float2(0, texel.y), slice), duv_dx, duv_dy).r;
                s.normal = normalize(normal - material.bump_strength * ((h_right - h_left) * tangent + (h_up - h_down) * bitangent));
            }
            else
            {
                float3 tangent_space = MaterialTextures.SampleGrad(MaterialSampler, float3(input.uvs, slice), duv_dx, duv_dy).xyz * 2.0f - 1.0f;
                s.normal = normalize(tangent_space.x * tangent + tangent_space.y * bitangent + tangent_space.z * normal);
            }
        }
#ifdef GDR_SHADER_FOLIAGE
        // Double sided: back faces are lit as the mirror image of the front (the shading normal reflected through the
        // surface plane), so leaves and cards are lit from whichever side the viewer sees
        if (!is_front_face)
        {
            s.normal -= 2.0f * dot(s.normal, normal) * normal;
        }
#endif
    }
#endif

    switch (GetDebugRenderMode())
    {
    case DebugRenderMode::SortOrder:
        return float4((1.0f - input.sort_position).xxx, 1.0f);
    case DebugRenderMode::Albedo:
        return float4(pow(saturate(s.albedo), 1.0f / 2.2f), 1.0f);
    case DebugRenderMode::Normals:
        return float4(s.normal * 0.5f + 0.5f, 1.0f);
    case DebugRenderMode::Lighting:
        s.albedo = float3(0.5f, 0.5f, 0.5f);
        break;
    default:
        break;
    }
    return float4(LinearToDisplay(ApplyLighting(s, input.world_position)), 1.0f);
}
