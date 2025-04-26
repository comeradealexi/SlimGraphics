Buffer<float4> buffer;
Buffer<unorm float4> buffer_unorm;
Buffer<snorm float4> buffer_snorm;

StructuredBuffer<float4> structured_buffer;
StructuredBuffer<unorm float4> structured_buffer_unorm;
StructuredBuffer<snorm float4> structured_buffer_snorm;

RWStructuredBuffer<uint> output_uav : register(u0);

[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    output_uav[0] = float4(0.25,0.5,0.75,1);
}