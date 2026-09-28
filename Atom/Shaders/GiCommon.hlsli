#ifndef ATOM_GI_COMMON
#define ATOM_GI_COMMON

cbuffer GiConstants : register(b0)
{
    float4x4 gProjection;
    float4x4 gInverseProjection;
    float4 gScreenSize; // width, height, inverse width, inverse height
    float4 gTrace;      // radius, thickness, ray count, steps per ray
    float4 gControl;    // jitter x/y in NDC, intensity, frame index
};

Texture2D<float4> gSceneColor : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gSceneNormal : register(t2);
Texture2D<float4> gDiffuseWeight : register(t3);
Texture2D<float4> gGiHalf : register(t4);
RWTexture2D<float4> gOutput : register(u0);

float2 PixelUV(uint2 pixel)
{
    return (float2(pixel) + 0.5) * gScreenSize.zw;
}

float3 ViewPosition(float2 uv, float depth)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0) - gControl.xy;
    float4 p = mul(gInverseProjection, float4(ndc, depth, 1.0));
    return p.xyz / p.w;
}

float2 ProjectView(float3 p)
{
    float4 clip = mul(gProjection, float4(p, 1.0));
    float2 ndc = clip.xy / clip.w + gControl.xy;
    return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

uint GiHash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    value ^= value >> 16;
    return value;
}

float GiRandom(uint seed)
{
    return float(GiHash(seed) & 0x00ffffffu) * (1.0 / 16777216.0);
}

#endif
