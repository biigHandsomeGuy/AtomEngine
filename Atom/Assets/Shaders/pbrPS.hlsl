#include "PBRCommon.hlsli"


Texture2D gAlbedeTexture : register(t0);
Texture2D gNormalTexture : register(t1);
Texture2D gMetalnessTexture : register(t2);
Texture2D gRoughnessTexture : register(t3);

Texture2D gSphereMap : register(t10);
Texture2D gShadowMap : register(t11);
Texture2D gSsaoMap : register(t12);
TextureCube gCubeMap : register(t13);

TextureCube gIrradianceMap : register(t14);
TextureCube gSpecularMap : register(t15);
Texture2D<float2> gLUTMap : register(t16);
SamplerState gsamLinearWrap : register(s3);

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

cbuffer ShaderParams : register(b2)
{
    bool UseSSAO = false;
    bool UseShadow = false;
    bool UseTexture = false;

    float Roughness;
    float3 Albedo;
    float Metallic;
   
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

float4 main(VertexOut pin) : SV_Target
{
    float3 albedo = 0;
    float metalness = 0;
    float roughness = 0;
    float3 N = 0;
    if (UseTexture)
    {   
        // Sample input textures to get shading model params.
        albedo = pow(gAlbedeTexture.Sample(gsamAnisotropicWrap, pin.TexC).rgb, 2.2);
        metalness = gMetalnessTexture.Sample(gsamAnisotropicWrap, pin.TexC).r;
        roughness = gRoughnessTexture.Sample(gsamAnisotropicWrap, pin.TexC).r;
        // Get current fragment's normal and transform to world space.
        N = normalize(2.0 * gNormalTexture.Sample(gsamAnisotropicWrap, pin.TexC).rgb - 1.0);
	
        N = normalize(mul(pin.tangentBasis, N));
    }
	else
    {
        albedo = Albedo;
        metalness = Metallic;
        roughness = Roughness;
        N = normalize(pin.Normal);
        //return float4(N, 1);
    }
    
	// Outgoing light direction (vector from world-space fragment position to the "eye").
    float3 Lo = normalize(gCameraPos - pin.PosW);

	
	// Angle between surface normal and outgoing light direction.
    float cosLo = max(0.0, dot(N, Lo));
		
	// Specular reflection vector.
    float3 Lr = reflect(-Lo, N);
    
	// Fresnel reflectance at normal incidence (for metals use albedo color).
    float3 F0 = lerp(Fdielectric, albedo, metalness);

	// Direct lighting calculation for analytical lights.
    float3 directLighting = 0.0;
    for (uint i = 0; i < 1; ++i)
    {
        float3 Li = normalize(gSunPosition - pin.PosW);
        float3 Lradiance = { 1,1,1 };

		// Half-vector between Li and Lo.
        float3 Lh = normalize(Li + Lo);

		// Calculate angles between surface normal and various light vectors.
        float cosLi = max(0.0, dot(N, Li));
        float cosLh = max(0.0, dot(N, Lh));

		// Calculate Fresnel term for direct lighting. 
        float3 F = F_Schlick(F0, max(0.0, dot(Lh, Lo)));
		// Calculate normal distribution for specular BRDF.
        float D = D_GGX(roughness, max(0.0, dot(Lh, N)));
		// Calculate geometric attenuation for specular BRDF.
        float G = G_Smith(N, Lo, Li, roughness);

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
        directLighting += (diffuseBRDF + specularBRDF) * Lradiance * cosLi;
    }

	// Ambient lighting (IBL).
    float3 ambientLighting;
	{
		// Sample diffuse irradiance at normal direction.
        float3 irradiance = gIrradianceMap.Sample(gsamAnisotropicWrap, N).rgb;

		// Calculate Fresnel term for ambient lighting.
		// Since we use pre-filtered cubemap(s) and irradiance is coming from many directions
		// use cosLo instead of angle with light's half-vector (cosLh above).
		// See: https://seblagarde.wordpress.com/2011/08/17/hello-world/
        float3 F = F_Schlick(F0, cosLo);

		// Get diffuse contribution factor (as with direct lighting).
        float3 kd = lerp(1.0 - F, 0.0, metalness);

		// Irradiance map contains exitant radiance assuming Lambertian BRDF, no need to scale by 1/PI here either.
        float3 diffuseIBL = kd * albedo * irradiance;

		// Sample pre-filtered specular reflection environment at correct mipmap level.
        uint specularTextureLevels = querySpecularTextureLevels();
        float3 specularIrradiance = gSpecularMap.SampleLevel(gsamLinearWrap, Lr, roughness * specularTextureLevels).rgb;

		// Split-sum approximation factors for Cook-Torrance specular BRDF.
        float2 specularBRDF = gLUTMap.Sample(gsamLinearWrap, float2(cosLo, roughness)).rg;

		// Total specular IBL contribution.
        float3 specularIBL = (1 - kd)*
        (F0 * specularBRDF.x + specularBRDF.y) * specularIrradiance;

		// Total ambient lighting contribution.
        ambientLighting = diffuseIBL + specularIBL;
    }

    float ambientOcclution = 1;
    if(UseSSAO)
    {
        pin.SsaoPosH /= pin.SsaoPosH.w;
        ambientOcclution = gSsaoMap.Sample(gsamAnisotropicWrap, pin.SsaoPosH.xy);
    }
    
	// Final fragment color.
    return float4(directLighting + ambientOcclution * ambientLighting, 1.0);
}
 
