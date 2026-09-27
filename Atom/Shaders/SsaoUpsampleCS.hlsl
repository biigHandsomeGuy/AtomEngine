Texture2D gNormalMap : register(t0);
Texture2D gDepthMap : register(t1);
Texture2D gInputMap : register(t2);

SamplerState gsamPointClamp : register(s0);
SamplerState gsamLinearClamp : register(s1);
SamplerState gsamDepthMap : register(s2);
SamplerState gsamLinearWrap : register(s3);
RWTexture2D<float4> gOutputMap : register(u0);

cbuffer cbSsao : register(b0)
{
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gProjTex;
    float4 gOffsetVectors[14];
    float4 gBlurWeights[3];
    float2 gInvRenderTargetSize;
    float gOcclusionRadius;
    float gOcclusionFadeStart;
    float gOcclusionFadeEnd;
    float gSurfaceEpsilon;
};

cbuffer cbRootConstants : register(b1)
{
    bool gHorizontalBlur;
};

float ViewDepthFromUv(float2 uv)
{
    float depth = gDepthMap.SampleLevel(gsamDepthMap, uv, 0.0f).r;
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 clip = float4(ndc, depth, 1.0f);
    float4 view = mul(gInvProj, clip);
    return view.z / max(view.w, 1e-6f);
}

float ComputeWeight(float3 centerN, float centerZ, float2 uv)
{
    float3 n = normalize(gNormalMap.SampleLevel(gsamPointClamp, uv, 0.0f).xyz);
    float z = ViewDepthFromUv(uv);
    float normalW = pow(saturate(dot(centerN, n)), 8.0f);
    float depthW = saturate(1.0f - abs(z - centerZ) / 0.75f);
    return normalW * depthW;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint outW, outH;
    gOutputMap.GetDimensions(outW, outH);
    if (DTid.x >= outW || DTid.y >= outH)
    {
        return;
    }

    float2 outUv = (float2(DTid.xy) + 0.5f) / float2(outW, outH);
    float3 centerNormal = normalize(gNormalMap.SampleLevel(gsamPointClamp, outUv, 0.0f).xyz);
    float centerDepth = ViewDepthFromUv(outUv);

    uint inW, inH;
    gInputMap.GetDimensions(inW, inH);

    float2 inPix = outUv * float2(inW, inH) - 0.5f;
    int2 base = int2(floor(inPix));
    float2 fracPart = frac(inPix);

    float totalW = 0.0f;
    float totalAo = 0.0f;

    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            int2 p = clamp(base + int2(x, y), int2(0, 0), int2((int)inW - 1, (int)inH - 1));
            float2 uv = (float2(p) + 0.5f) / float2(inW, inH);

            float bilinearW = ((x == 0) ? (1.0f - fracPart.x) : fracPart.x) *
                              ((y == 0) ? (1.0f - fracPart.y) : fracPart.y);
            float edgeW = ComputeWeight(centerNormal, centerDepth, uv);
            float w = bilinearW * edgeW;
            float ao = gInputMap.Load(int3(p, 0)).r;

            totalAo += ao * w;
            totalW += w;
        }
    }

    float outAo = (totalW > 1e-5f) ? (totalAo / totalW) : gInputMap.SampleLevel(gsamLinearClamp, outUv, 0.0f).r;
    gOutputMap[DTid.xy] = float4(saturate(outAo), 0.0f, 0.0f, 1.0f);
}
