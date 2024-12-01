
Texture2D gAlbedeTexture[2] : register(t0);
Texture2D gNormalTexture[2] : register(t2);
Texture2D gMetalnessTexture[2] : register(t4);
Texture2D gRoughnessTexture[2] : register(t6);

Texture2D gSphereMap : register(t10);
Texture2D gShadowMap : register(t11);
Texture2D gSsaoMap : register(t12);
TextureCube gCubeMap : register(t13);

TextureCube gIrradianceMap : register(t14);
TextureCube gSpecularMap : register(t15);
Texture2D<float2> gLUTMap : register(t16);
SamplerState gsamLinearWrap : register(s2);

SamplerComparisonState gsamShadow : register(s6);
cbuffer MaterialConstants : register(b0)
{
    uint gMatIndex;
};

cbuffer GlobalConstants : register(b1)
{
    float4x4 gView;
    float4x4 gProj;
    float4x4 gViewProj;
    float4x4 gSunShadowMatrix;
    float3 gCameraPos;
    float pad;
    float3 gSunPosition;

};

cbuffer ShaderParams
{
    bool UseSSAO;
    bool UseShadow;
    uint DebugView;
};
SamplerState gsamAnisotropicWrap : register(s4);
static const float3 Fdielectric = 0.04;
static const float Epsilon = 0.00001;

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 ShadowPosH : POSITION0;
    float4 SsaoPosH : POSITION1;
    float3 PosW : POSITION2;
    float2 TexC : TEXCOORD;
    float3x3 tangentBasis : TAASIC;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
};

// Returns number of mipmap levels for specular IBL environment map.
uint querySpecularTextureLevels()
{
    uint width, height, levels;
    gSpecularMap.GetDimensions(0, width, height, levels);
    return levels;
}

float DistributionGGX(float3 N, float3 H, float roughness);
float GeometrySchlickGGX(float NdotV, float roughness);
float GeometrySmith(float3 N, float3 V, float3 L, float roughness);
float3 fresnelSchlick(float cosTheta, float3 F0);
float CalcShadowFactorPCSS(float4 shadowPosH);

float CalcShadowFactor(float4 shadowPosH);


// enum
// class DebugViewType : UINT8
// {
//     None,
//     BaseColor,
//     Metallic,
//     Roughness,
//     DiffuseColor,
//     SpecularColor,
//     AmbientLight,
//     DirectLight,
//     DebugAO,
// };


float4 main(VertexOut pin) : SV_Target
{
    
    float3 albedo = pow(gAlbedeTexture[gMatIndex].Sample(gsamAnisotropicWrap, pin.TexC).rgb, 2.2);
    if (DebugView == 1)
        return float4(albedo, 1);
    float metalness = gMetalnessTexture[gMatIndex].Sample(gsamAnisotropicWrap, pin.TexC).r;
    if (DebugView == 2)
        return float4(metalness.xxx, 1);
    float roughness = gRoughnessTexture[gMatIndex].Sample(gsamAnisotropicWrap, pin.TexC).r;
    if (DebugView == 3)
        return float4(roughness.xxx, 1);
	// Outgoing light direction (vector from world-space fragment position to the "eye").
    float3 Lo = normalize(gCameraPos - pin.PosW);

    // Get current fragment's normal and transform to world space.
    float3 N = normalize(2 * gNormalTexture[gMatIndex].Sample(gsamAnisotropicWrap, pin.TexC).rgb - 1);
    N = normalize(mul(pin.tangentBasis, N));
    
    // Angle between surface normal and outgoing light direction.
    float cosLo = max(0.0, dot(N, Lo));
    
    // Specular reflection vector.
    float3 Lr = 2.0 * cosLo * N - Lo;

    // Fresnel reflectance at normal incidence (for metals use albedo color).
    float3 F0 = lerp(Fdielectric, albedo, metalness);
    
    float ambientAccess = 1;
    if (UseSSAO)
    {
        // Finish texture projection and sample SSAO map.
        pin.SsaoPosH /= pin.SsaoPosH.w;
        ambientAccess = gSsaoMap.Sample(gsamLinearWrap, pin.SsaoPosH.xy, 0.0f).r;
        
        if (DebugView == 8)
            return float4(ambientAccess.xxx, 1);

    }
  
    // Direct lighting calculation for analytical lights.
    float3 directLighting = 0.0;
    {
        float3 Li = normalize(gSunPosition - pin.PosW);
        float3 radiance = float3(1.0f,1.0f,1.0f);
        
        // Half-vector between Li and Lo.
        float3 H = normalize(Lo + Li);
        
        // Calculate angles between surface normal and various light vectors.
        float cosLi = max(0.0, dot(N, Li));
        float cosLh = max(0.0, dot(N, H));

        
        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, Lo, Li, roughness);
        float3 F = fresnelSchlick(max(dot(H, Lo), 0.0), F0);
    
        // Diffuse scattering happens due to light being refracted multiple times by a dielectric medium.
		// Metals on the other hand either reflect or absorb energy, so diffuse contribution is always zero.
		// To be energy conserving we must scale diffuse BRDF contribution based on Fresnel factor & metalness.
        float3 kd = lerp(float3(1, 1, 1) - F, float3(0, 0, 0), metalness);

		// Lambert diffuse BRDF.
		// We don't scale by 1/PI for lighting & material units to be more convenient.
		// See: https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
        float3 diffuseBRDF = kd * albedo;

		// Cook-Torrance specular microfacet BRDF.
        float3 specularBRDF = (F * D * G) / max(Epsilon, 4.0 * cosLi * cosLo);

		// Total contribution for this light.
        directLighting += (diffuseBRDF + specularBRDF) * radiance * cosLi;
        
        if (DebugView == 7)
            return float4(directLighting, 1);

    }
    // Only the first light casts a shadow.
    float shadowFactor = 1;
     
    if (UseShadow)
    {
        shadowFactor = CalcShadowFactor(pin.ShadowPosH);
    }

    float3 ambientLighting = 0;
    {
        float3 irradiance = gIrradianceMap.Sample(gsamAnisotropicWrap, N).rgb;
        
        float3 F = fresnelSchlick(cosLo, F0);
        
        // Get diffuse contribution factor (as with direct lighting).
        float3 kd = lerp(1.0 - F, 0.0, metalness);
        
        // Irradiance map contains exitant radiance assuming Lambertian BRDF, no need to scale by 1/PI here either.
        float3 diffuseIBL = kd * albedo * irradiance;
        
        if (DebugView == 4)
            return float4(diffuseIBL, 1);
        
        // Sample pre-filtered specular reflection environment at correct mipmap level.
        uint specularTextureLevels = querySpecularTextureLevels();
        float3 specularIrradiance = gSpecularMap.SampleLevel(gsamAnisotropicWrap, Lr, roughness * specularTextureLevels).rgb;

		// Split-sum approximation factors for Cook-Torrance specular BRDF.
        float2 specularBRDF = gLUTMap.Sample(gsamAnisotropicWrap, float2(roughness, cosLo)).rg;
        
		// Total specular IBL contribution.
        float3 specularIBL = (1 - kd) *
        (F0 * specularBRDF.x + specularBRDF.y) * specularIrradiance;

        if (DebugView == 5)
            return float4(specularIBL, 1);
        
        
        ambientLighting = specularIBL + diffuseIBL;
        if (DebugView == 6)
            return float4(ambientLighting, 1);

    }
    
    float3 color = directLighting * shadowFactor + ambientLighting * ambientAccess;

    
    color = color / (color + 1);
    color = pow(color, 1.0f / 2.2f);

    return float4(color, 1.0);
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


float CalcShadowFactor(float4 shadowPosH)
{
    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;

    // Depth in NDC space.
    float depth = shadowPosH.z;

    uint width, height, numMips;
    gShadowMap.GetDimensions(0, width, height, numMips);

    // Texel size.
    float dx = 1.0f / (float) width;

    float percentLit = 0.0f;
    const float2 offsets[9] =
    {
        float2(-dx, -dx), float2(0.0f, -dx), float2(dx, -dx),
        float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
        float2(-dx, +dx), float2(0.0f, +dx), float2(dx, +dx)
    };

    [unroll]
    for (int i = 0; i < 9; ++i)
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
