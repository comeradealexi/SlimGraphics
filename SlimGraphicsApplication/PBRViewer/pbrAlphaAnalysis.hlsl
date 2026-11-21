// Scene loading: counts texels of a base colour texture below the alpha cutoff on a 64 x 64 grid. Materials whose textures
// have cut outs are alpha tested (FBX and OBJ files do not say; compressed DDS alpha can only be read by the GPU).

struct AlphaConstants
{
    uint texture_index;     // Bindless
    uint output;
    float cutoff;
    uint pad;
};
ConstantBuffer<AlphaConstants> constants : register(b0);
RWStructuredBuffer<uint> AlphaCounts : register(u0);
SamplerState TrilinearSampler : register(s3);

[numthreads(8, 8, 1)]
void analyse_alpha(uint3 dtid : SV_DispatchThreadID)
{
    Texture2D<float4> t = ResourceDescriptorHeap[constants.texture_index];
    float w, h;
    t.GetDimensions(w, h);
    float2 uv = (dtid.xy + 0.5f) / 64.0f;
    float level = max(log2(max(w, h) / 64.0f), 0.0f);
    float alpha = t.SampleLevel(TrilinearSampler, uv, level).a;
    if (alpha < constants.cutoff)
    {
        InterlockedAdd(AlphaCounts[constants.output], 1);
    }
}
