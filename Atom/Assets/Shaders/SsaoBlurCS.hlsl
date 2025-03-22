// SSAO Blur Compute Shader
// �߳̿��С
#define BLOCK_SIZE 16

// ����Ͳ�����
Texture2D gNormalMap : register(t0);
Texture2D gDepthMap : register(t1);
Texture2D gInputMap : register(t2);

SamplerState gsamPointClamp : register(s0);
SamplerState gsamLinearClamp : register(s1);
SamplerState gsamDepthMap : register(s2);
SamplerState gsamLinearWrap : register(s3);
// �������Unordered Access View��
RWTexture2D<float4> gOutputMap : register(u0);

cbuffer cbSsao : register(b0)
{
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gProjTex;
    float4 gOffsetVectors[14];

    // For SsaoBlur.hlsl
    float4 gBlurWeights[3];

    float2 gInvRenderTargetSize;

    // Coordinates given in view space.
    float gOcclusionRadius;
    float gOcclusionFadeStart;
    float gOcclusionFadeEnd;
    float gSurfaceEpsilon;

    
};

static const int gBlurRadius = 5;

// NDC ���ת��Ϊ View ���
float NdcDepthToViewDepth(float z_ndc)
{
    return gProj[3][2] / (z_ndc - gProj[2][2]);
}

// �߳��鶨��
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 texC = (DTid.xy + 0.5f) * gInvRenderTargetSize;

    // unpack into float array.
    float blurWeights[12] =
    {
        gBlurWeights[0].x, gBlurWeights[0].y, gBlurWeights[0].z, gBlurWeights[0].w,
        gBlurWeights[1].x, gBlurWeights[1].y, gBlurWeights[1].z, gBlurWeights[1].w,
        gBlurWeights[2].x, gBlurWeights[2].y, gBlurWeights[2].z, gBlurWeights[2].w,
    };

    float4 color = blurWeights[gBlurRadius] * gInputMap.SampleLevel(gsamPointClamp, texC, 0);
    float totalWeight = blurWeights[gBlurRadius];

    float3 centerNormal = gNormalMap.SampleLevel(gsamPointClamp, texC, 0).xyz;
    float centerDepth = NdcDepthToViewDepth(gDepthMap.SampleLevel(gsamDepthMap, texC, 0).r);

    // ˫��ģ��
    for (int y = -gBlurRadius; y <= gBlurRadius; ++y)
    {
        for (int x = -gBlurRadius; x <= gBlurRadius; ++x)
        {
            if (x == 0 && y == 0)
                continue; // �������ĵ�

            float2 offset = float2(x * gInvRenderTargetSize.x, y * gInvRenderTargetSize.y);
            float2 tex = texC + offset;

            float3 neighborNormal = gNormalMap.SampleLevel(gsamPointClamp, tex, 0).xyz;
            float neighborDepth = NdcDepthToViewDepth(gDepthMap.SampleLevel(gsamDepthMap, tex, 0).r);

            if (dot(neighborNormal, centerNormal) >= 0.8f && abs(neighborDepth - centerDepth) <= 0.2f)
            {
                float weight = blurWeights[abs(x) + abs(y)];
                color += weight * gInputMap.SampleLevel(gsamPointClamp, tex, 0);
                totalWeight += weight;
            }
        }
    }

    // ��һ��
    gOutputMap[DTid.xy] = color / totalWeight;
}