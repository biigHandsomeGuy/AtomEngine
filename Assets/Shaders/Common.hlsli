#ifndef COMMON_HLSLI
#define COMMON_HLSLI

Texture2D gSphereMap : register(t0);
Texture2D gShadowMap : register(t1);
Texture2D gSsaoMap   : register(t2);

// An array of textures, which is only supported in shader model 5.1+.  Unlike Texture2DArray, the textures
// in this array can be different sizes and formats, making it more flexible than texture arrays.
// Texture2D gTextureMaps[10] : register(t3);

Texture2D gAlbedeTexture : register(t3);
Texture2D gNormalTexture : register(t4);
Texture2D gMetalnessTexture : register(t5);
Texture2D gRoughnessTexture : register(t6);
TextureCube gCubeMap : register(t13);
TextureCube gIrradianceMap : register(t14);
TextureCube gSpecularMap : register(t15);
Texture2D<float2> gLUTMap : register(t16);
// Put in space1, so the texture array does not overlap with these resources.  
// The texture array will occupy registers t0, t1, ..., t3 in space0. 
//StructuredBuffer<MaterialData> gMaterialData : register(t0, space1);


SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
};
struct Light
{
    float3 Strength;
    float FalloffStart; // point/spot light only
    float3 Position; // point light only
    float FalloffEnd; // point/spot light only
};
// Constant data that varies per material.
cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4x4 gViewProjTex;
    float4x4 gShadowTransform;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light gLights[3];
};

cbuffer cb : register(b2)
{
    float gRoughness;
}

cbuffer mips : register(b3)
{
    uint mips;
}


//---------------------------------------------------------------------------------------
// Transforms a normal map sample to world space.
//---------------------------------------------------------------------------------------
float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
	// Uncompress each component from [0,1] to [-1,1].
	float3 normalT = 2.0f*normalMapSample - 1.0f;

	// Build orthonormal basis.
	float3 N = unitNormalW;
	float3 T = normalize(tangentW - dot(tangentW, N)*N);
	float3 B = cross(N, T);

	float3x3 TBN = float3x3(T, B, N);

	// Transform from tangent space to world space.
	float3 bumpedNormalW = mul(normalT, TBN);

	return bumpedNormalW;
}

float CalcShadowFactor(float4 shadowPosH)
{
    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;

    // Depth in NDC space.
    float depth = shadowPosH.z;

    uint width, height, numMips;
    gShadowMap.GetDimensions(0, width, height, numMips);

    // Texel size.
    float dx = 1.0f / (float)width;

    float percentLit = 0.0f;
    const float2 offsets[9] =
    {
        float2(-dx,  -dx), float2(0.0f,  -dx), float2(dx,  -dx),
        float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
        float2(-dx,  +dx), float2(0.0f,  +dx), float2(dx,  +dx)
    };

    [unroll]
    for(int i = 0; i < 9; ++i)
    {
        percentLit += gShadowMap.SampleCmpLevelZero(gsamShadow,
            shadowPosH.xy + offsets[i], depth).r;
    }
    return gShadowMap.SampleCmpLevelZero(gsamShadow,
            shadowPosH.xy, depth).r;
    return percentLit / 9.0f;
}

float CalcShadowFactorPCSS(float4 shadowPosH)
{
    float shadowFactor = 1.0f;
    const float lightSize = 40.0f;

    // 完成投影
    shadowPosH.xyz /= shadowPosH.w;

    // 获取深度
    float depth = shadowPosH.z;

    uint width, height, numMips;
    gShadowMap.GetDimensions(0, width, height, numMips);

    // 纹理像素大小
    float dx = 1.0f / (float) width;
    float dy = 1.0f / (float) height;

    // 遮挡物深度搜索 (Blocker Search)
    float blockerDepth = 0.0f;
    float blockerNum = 0.0f;
    
    const int blockerSearchRadius = 3; // 这是用于遮挡物搜索的固定 3x3 采样
    for (int i = -blockerSearchRadius; i <= blockerSearchRadius; ++i)
    {
        for (int j = -blockerSearchRadius; j <= blockerSearchRadius; ++j)
        {
            float shadowMapDepth = gShadowMap.Sample(gsamLinearWrap, shadowPosH.xy + dx * 5 * float2(i, j)).r;
            if (depth > shadowMapDepth)
            {
                blockerDepth += shadowMapDepth;
                blockerNum += 1.0f;
            }
        }
    }
    
    if (blockerNum > 0.0f)
    {
        blockerDepth /= blockerNum;
        
        // 半影范围计算 (Penumbra Calculation)
        float penumbra = clamp((depth - blockerDepth) * lightSize / blockerDepth, 0.0f, 50.0f); // 限制penumbra最大值
        
        // 根据 penumbra 动态调整采样范围
        int pcfSampleRadius = int(ceil(penumbra)); // 动态调整 PCF 样本半径，基于 penumbra
        
        shadowFactor = 0.0f;
        int pcfSamples = 0;

        // 动态调整的 PCF 采样，基于 penumbra 的大小
        for (int i = -pcfSampleRadius; i <= pcfSampleRadius; ++i)
        {
            for (int j = -pcfSampleRadius; j <= pcfSampleRadius; ++j)
            {
                float2 offset = float2(i * dx, j * dy); // 根据 penumbra 扩展采样范围
                shadowFactor += gShadowMap.SampleCmpLevelZero(gsamShadow, shadowPosH.xy + offset, depth).r;
                pcfSamples++;
            }
        }

        // 归一化阴影因子
        shadowFactor /= (float) pcfSamples;
    }

    return shadowFactor;
}

// ----------------------------------------------------------------------------
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159 * denom * denom;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
// ----------------------------------------------------------------------------
float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

#endif // COMMON_HLSLI