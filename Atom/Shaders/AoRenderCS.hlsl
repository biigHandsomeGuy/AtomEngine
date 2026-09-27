Texture2D<float4> gNormalBuffer : register(t0);
Texture2D<float> gLinearDepthHalf : register(t1);
Texture2D<float4> gRandomBuffer : register(t2);
RWTexture2D<float> gAoBuffer : register(u0);

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

float3 ReconstructViewPos(float2 uv, float linearDepth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float2 invProjScale = float2(rcp(gProj[0][0]), rcp(gProj[1][1]));
    float2 viewXY = ndc * linearDepth * invProjScale;
    return float3(viewXY, -linearDepth);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint outW, outH;
    gAoBuffer.GetDimensions(outW, outH);
    if (DTid.x >= outW || DTid.y >= outH)
    {
        return;
    }

    float2 uv = (float2(DTid.xy) + 0.5f) / float2(outW, outH);
    float centerDepth = gLinearDepthHalf.Load(int3(DTid.xy, 0));
    if (centerDepth <= 0.0f)
    {
        gAoBuffer[DTid.xy] = 1.0f;
        return;
    }

    float3 normal = normalize(gNormalBuffer.SampleLevel(gsamLinearClamp, uv, 0.0f).xyz);
    float3 centerPos = ReconstructViewPos(uv, centerDepth);

    float3 randomVec = normalize(gRandomBuffer.SampleLevel(gsamLinearClamp, uv * 8.0f, 0.0f).xyz * 2.0f - 1.0f);
    float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    float tangentLen = length(tangent);
    if (tangentLen < 1.0e-4f)
    {
        tangent = normalize(cross(abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f), normal));
    }
    float3 bitangent = normalize(cross(normal, tangent));

    float radius = gAoParams.x;
    float power = gAoParams.y;
    float bias = gAoParams.z;
    float thickness = gAoParams.w;

    float occlusion = 0.0f;

    [unroll]
    for (uint i = 0; i < 16; ++i)
    {
        float3 sampleDir =
            tangent * gSampleKernel[i].x +
            bitangent * gSampleKernel[i].y +
            normal * gSampleKernel[i].z;

        float3 samplePos = centerPos + sampleDir * radius;
        float4 clip = mul(gProj, float4(samplePos, 1.0f));
        if (abs(clip.w) < 1.0e-5f)
        {
            continue;
        }

        float2 sampleUv = clip.xy / clip.w;
        sampleUv = sampleUv * float2(0.5f, -0.5f) + 0.5f;
        if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f))
        {
            continue;
        }

        float sampleDepth = gLinearDepthHalf.SampleLevel(gsamPointClamp, sampleUv, 0.0f);
        if (sampleDepth <= 0.0f)
        {
            continue;
        }

        float3 sampleViewPos = ReconstructViewPos(sampleUv, sampleDepth);
        float3 delta = sampleViewPos - centerPos;
        float deltaLen = length(delta);
        if (deltaLen < 1.0e-4f)
        {
            continue;
        }

        float rangeWeight = saturate(1.0f - deltaLen / max(radius * thickness, 1.0e-3f));
        float alignment = saturate(dot(normal, delta / deltaLen));
        float samplePosDepth = -samplePos.z;
        float blocked = sampleDepth < (samplePosDepth - bias) ? 1.0f : 0.0f;

        occlusion += blocked * alignment * rangeWeight;
    }

    float ao = saturate(1.0f - occlusion / 16.0f);
    gAoBuffer[DTid.xy] = saturate(pow(ao, power));
}
