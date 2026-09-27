Texture2D<float4> gNormalBuffer : register(t0);
Texture2D<float> gLinearDepthFull : register(t1);
Texture2D<float> gLinearDepthHalf : register(t2);
Texture2D<float> gInputAo : register(t3);
RWTexture2D<float> gOutputAo : register(u0);

SamplerState gsamPointClamp : register(s0);
SamplerState gsamLinearClamp : register(s1);

cbuffer SsaoCB : register(b0)
{
    float4x4 gProj;
    float4x4 gInvProj;
    float4 gSampleKernel[16];
    float4 gInvSize;
    float4 gAoParams;
};

cbuffer RootConstants : register(b1)
{
    uint gPassMode;
};

float EdgeWeight(float2 centerUv, float centerDepth, float3 centerNormal, float2 sampleUv, float sampleDepth)
{
    float3 sampleNormal = normalize(gNormalBuffer.SampleLevel(gsamLinearClamp, sampleUv, 0.0f).xyz);
    float normalW = pow(saturate(dot(centerNormal, sampleNormal)), 8.0f);
    float depthSigma = max(0.1f, centerDepth * 0.02f);
    float depthW = saturate(1.0f - abs(sampleDepth - centerDepth) / depthSigma);
    return normalW * depthW;
}

float BlurHalfRes(int2 pixel, uint outW, uint outH)
{
    float2 uv = (float2(pixel) + 0.5f) / float2(outW, outH);
    float centerDepth = gLinearDepthHalf.Load(int3(pixel, 0));
    float3 centerNormal = normalize(gNormalBuffer.SampleLevel(gsamLinearClamp, uv, 0.0f).xyz);

    static const float kernel[9] =
    {
        1.0f, 2.0f, 1.0f,
        2.0f, 4.0f, 2.0f,
        1.0f, 2.0f, 1.0f
    };

    float totalAo = 0.0f;
    float totalW = 0.0f;
    uint idx = 0;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 p = clamp(pixel + int2(x, y), int2(0, 0), int2((int)outW - 1, (int)outH - 1));
            float2 sampleUv = (float2(p) + 0.5f) / float2(outW, outH);
            float sampleDepth = gLinearDepthHalf.Load(int3(p, 0));
            float w = kernel[idx++] * EdgeWeight(uv, centerDepth, centerNormal, sampleUv, sampleDepth);
            float ao = gInputAo.Load(int3(p, 0));
            totalAo += ao * w;
            totalW += w;
        }
    }

    return totalW > 1.0e-5f ? (totalAo / totalW) : gInputAo.Load(int3(pixel, 0));
}

float UpsampleFullRes(int2 pixel, uint outW, uint outH)
{
    float2 uv = (float2(pixel) + 0.5f) / float2(outW, outH);
    float centerDepth = gLinearDepthFull.Load(int3(pixel, 0));
    float3 centerNormal = normalize(gNormalBuffer.SampleLevel(gsamLinearClamp, uv, 0.0f).xyz);

    uint inW, inH;
    gInputAo.GetDimensions(inW, inH);

    float2 inputPixel = uv * float2(inW, inH) - 0.5f;
    int2 basePixel = int2(floor(inputPixel));
    float2 fracPart = frac(inputPixel);

    float totalAo = 0.0f;
    float totalW = 0.0f;

    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            int2 p = clamp(basePixel + int2(x, y), int2(0, 0), int2((int)inW - 1, (int)inH - 1));
            float2 sampleUv = (float2(p) + 0.5f) / float2(inW, inH);
            float bilinearW = ((x == 0) ? (1.0f - fracPart.x) : fracPart.x) *
                              ((y == 0) ? (1.0f - fracPart.y) : fracPart.y);
            float sampleDepth = gLinearDepthHalf.Load(int3(p, 0));
            float w = bilinearW * EdgeWeight(uv, centerDepth, centerNormal, sampleUv, sampleDepth);
            float ao = gInputAo.Load(int3(p, 0));
            totalAo += ao * w;
            totalW += w;
        }
    }

    return totalW > 1.0e-5f ? (totalAo / totalW) : gInputAo.SampleLevel(gsamLinearClamp, uv, 0.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint outW, outH;
    gOutputAo.GetDimensions(outW, outH);
    if (DTid.x >= outW || DTid.y >= outH)
    {
        return;
    }

    float ao = (gPassMode == 0) ? BlurHalfRes(int2(DTid.xy), outW, outH) : UpsampleFullRes(int2(DTid.xy), outW, outH);
    gOutputAo[DTid.xy] = saturate(ao);
}
