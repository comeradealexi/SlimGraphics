//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#ifdef ADD_CONSTANT_BUFFER
cbuffer ConstantBufferData : register(b0)
{
    float4 cb_colour;
};
#endif

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 UVs : TEXCOORD0;
};

PS_INPUT VSMain(uint id : SV_VertexID)
{
    PS_INPUT psOut;

    if (id == 4)
    {
        psOut.Position = float4(-1, -1, 1, 1);
        psOut.UVs = float2(0, 1);
    }
    else if (id == 3)
    {
        psOut.Position = float4(1, -1, 1, 1);
        psOut.UVs = float2(1, 1);
    }
    else if (id == 5)
    {
        psOut.Position = float4(1, 1, 1, 1);
        psOut.UVs = float2(1, 0);
    }
    else if (id == 0)
    {
        psOut.Position = float4(1, 1, 1, 1);
        psOut.UVs = float2(1, 0);
    }
    else if (id == 1)
    {
        psOut.Position = float4(-1, -1, 1, 1);
        psOut.UVs = float2(0, 1);
    }
    else //id = 2
    {
        psOut.Position = float4(-1, 1, 1, 1);
        psOut.UVs = float2(0, 0);
    }
    
    return psOut;
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
#ifdef ADD_CONSTANT_BUFFER
        return float4(input.UVs.x * cb_colour.x, input.UVs.y * cb_colour.y, cb_colour.z, 1);
#else    
    return float4(input.UVs.x, input.UVs.y, 0, 1);
#endif
}
