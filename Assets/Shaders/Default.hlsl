//***************************************************************************************
// Default.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

// Include common HLSL code.
#include "Common.hlsli"
static const float Epsilon = 0.00001;

static const float3 Fdielectric = 0.04;


struct VertexIn
{
	float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
	float3 Tangent : TANGENT;
	float3 BiTangent : BITANGENT;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
    float4 ShadowPosH : POSITION0;
    float4 SsaoPosH   : POSITION1;
    float3 PosW    : POSITION2;
	float2 TexC    : TEXCOORD;
    float3x3 tangentBasis : TAASIC;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;
    
    // Transform to world space.
    float4 posW = mul(gWorld, float4(vin.PosL, 1.0f));
    vout.PosW = posW.xyz;

    float3x3 TBN = float3x3(vin.Tangent, vin.BiTangent, vin.NormalL);       
    vout.tangentBasis = mul((float3x3) gWorld, transpose(TBN));
    
    // Transform to homogeneous clip space.
    //vout.PosH = mul(gViewProj, posW);
    vout.PosH = mul(gProj, mul(gView, posW));
    // Generate projective tex-coords to project SSAO map onto scene.
    vout.SsaoPosH = mul(posW, gViewProjTex);
	
	// Output vertex attributes for interpolation across triangle.
    vout.TexC = float2(vin.TexC.x, 1 - vin.TexC.y);

    vout.Normal = vin.NormalL;
    vout.Tangent = vin.Tangent;
    // Generate projective tex-coords to project shadow map onto scene.
    vout.ShadowPosH = mul(posW, gShadowTransform);
	
    return vout;
}
// Returns number of mipmap levels for specular IBL environment map.
uint querySpecularTextureLevels()
{
    uint width, height, levels;
    gSpecularMap.GetDimensions(0, width, height, levels);
    return levels;
}

float4 PS(VertexOut pin) : SV_Target
{
    float3 albedo = pow(gAlbedeTexture.Sample(gsamAnisotropicWrap, pin.TexC).rgb, 2.2);
    float metalness = gMetalnessTexture.Sample(gsamAnisotropicWrap, pin.TexC).r;
    float roughness = gRoughnessTexture.Sample(gsamAnisotropicWrap, pin.TexC).r;


	// Outgoing light direction (vector from world-space fragment position to the "eye").
    float3 Lo = normalize(gEyePosW - pin.PosW);

    // Get current fragment's normal and transform to world space.
    float3 N = normalize(2* gNormalTexture.Sample(gsamAnisotropicWrap, pin.TexC).rgb - 1);
    N = normalize(mul(pin.tangentBasis, N));
    //N = pin.Normal;
    // Angle between surface normal and outgoing light direction.
    float cosLo = max(0.0, dot(N, Lo));
    
    // Specular reflection vector.
    float3 Lr = 2.0 * cosLo * N - Lo;

    // Fresnel reflectance at normal incidence (for metals use albedo color).
    float3 F0 = lerp(Fdielectric, albedo, metalness);
    
#ifdef SSAO
    // Finish texture projection and sample SSAO map.
    pin.SsaoPosH /= pin.SsaoPosH.w;
    float ambientAccess = gSsaoMap.Sample(gsamLinearClamp, pin.SsaoPosH.xy, 0.0f).r;

#else
    float ambientAccess = 1;
#endif
    
    // Direct lighting calculation for analytical lights.
    float3 directLighting = 0.0;
    for (int i = 0; i < 3; i++)
    {
        float3 Li = normalize(gLights[i].Position - pin.PosW);     
        float3 radiance = gLights[i].Strength;
        
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
    }
    // Only the first light casts a shadow.
        float3 shadowFactor = float3(1.0f, 1.0f, 1.0f);
     
#ifdef PCSS
    shadowFactor[0] = CalcShadowFactorPCSS(pin.ShadowPosH);
#else
    shadowFactor[0] = CalcShadowFactor(pin.ShadowPosH);
#endif

    float3 ambientLighting = 0;
    {
        float3 irradiance = gIrradianceMap.Sample(gsamAnisotropicWrap, N).rgb;
        
        float3 F = fresnelSchlick(cosLo, F0);
        
        // Get diffuse contribution factor (as with direct lighting).
        float3 kd = lerp(1.0 - F, 0.0, metalness);
        
        // Irradiance map contains exitant radiance assuming Lambertian BRDF, no need to scale by 1/PI here either.
        float3 diffuseIBL = kd * albedo * irradiance;

        // Sample pre-filtered specular reflection environment at correct mipmap level.
        uint specularTextureLevels = querySpecularTextureLevels();
        float3 specularIrradiance = gSpecularMap.SampleLevel(gsamAnisotropicWrap, Lr, roughness * specularTextureLevels).rgb;

		// Split-sum approximation factors for Cook-Torrance specular BRDF.
        float2 specularBRDF = gLUTMap.Sample(gsamAnisotropicWrap, float2(roughness,cosLo )).rg;
        
		// Total specular IBL contribution.
        float3 specularIBL = (1 - kd) *
        (F0 * specularBRDF.x + specularBRDF.y) * specularIrradiance;

        
        ambientLighting = specularIBL + diffuseIBL;
        
    }
    
    float3 color = directLighting + ambientLighting;
    //color = albedo;
    
    color = color / (color +1);
    color = pow(color, 1.0f / 2.2f);
    //directLighting *= shadowFactor;
    return float4(N, 1.0);
}


