#include "Common.hlsli"
#include "PBRCommon.hlsli"


Texture2D gAlbedeTexture : register(t0);
Texture2D gRoughnessTexture : register(t1);
Texture2D gMetalnessTexture : register(t2);
Texture2D  gNormalTexture: register(t3);


TextureCube gEnvironmentTexture : register(t10);
TextureCube gRadianceTexture : register(t11);
TextureCube gIrradianceMap : register(t12);
Texture2D gSsaoMap : register(t13);
Texture2D gShadowMap : register(t14);
Texture2D<float2> gLUTMap : register(t15);
Texture2D<float4> gSSSDiffuseLUTMap : register(t16);
Texture2D<float4> gSSSSpecularLUTMap : register(t17);
Texture2D gEmu : register(t18);
Texture2D gEavg : register(t19);

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
    bool UseEmu;
    bool UseSSS;
    float CurveFactor;
    float SpecularFactor;
};
static const float3 g_Fdielectric = 0.04;

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 ShadowPosH : POSITION0;
    float4 SsaoPosH : POSITION1;
    float3 WorldPosition : POSITION2;
    float2 TexC : TEXCOORD;
    float3x3 tangentBasis : TAASIC;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
};

// Returns number of mipmap levels for specular IBL environment map.
uint querySpecularTextureLevels()
{
    uint width, height, levels;
    gRadianceTexture.GetDimensions(0, width, height, levels);
    return levels;
}

//https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
float3 AverageFresnel(float3 r, float3 g)
{
    return 0.087237 + 0.0230685 * g - 0.0864902 * g * g + 0.0774594 * g * g * g
           + 0.782654 * r - 0.136432 * r * r + 0.278708 * r * r * r
           + 0.19744 * g * r + 0.0360605 * g * g * r - 0.2586 * g * r * r;
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
	
        N = normalize(mul(N, pin.tangentBasis));
    }
	else
    {
        albedo = Albedo;
        metalness = Metallic;
        roughness = Roughness;
        N = normalize(pin.Normal);
        //return float4(N, 1);
    }
    //N = normalize(pin.Normal);
    //return float4(N, 1);
	// Outgoing light direction (vector from world-space fragment position to the "eye").
    float3 Lo = normalize(gCameraPos - pin.WorldPosition);
    
	
	// Angle between surface normal and outgoing light direction.
    float NoV = max(0.0, dot(N, Lo));

	// Specular reflection vector.
    float3 Lr = reflect(-Lo, N);
    
	// Fresnel reflectance at normal incidence (for metals use albedo color).
    float3 F0 = lerp(g_Fdielectric, albedo, metalness);

	// Direct lighting calculation for analytical lights.
    float3 directLighting = 0.0;
    for (uint i = 0; i < 1; ++i)
    {
        float alphaRoughness = roughness * roughness;
        
        float3 Li = normalize(gSunPosition - pin.WorldPosition);
        float3 Lradiance = { 1, 1, 1 };

		// Half-vector between Li and Lo.
        float3 Lh = normalize(Li + Lo);

		// Calculate angles between surface normal and various light vectors.
        float NoL = max(0.0, dot(N, Li));
        float NoH = max(0.0, dot(N, Lh));

        if(NoL > 0.0f || NoV > 0.0f)
		{
            // Calculate Fresnel term for direct lighting. 
            float3 F = F_Schlick(F0, max(0.0, dot(Lh, Lo)));
		    // Calculate normal distribution for specular BRDF.
            float D = D_GGX(NoH, alphaRoughness);
		    // Calculate geometric attenuation for specular BRDF.
            float G = V_SmithGGXCorrelated(NoL, NoV, alphaRoughness);

		    // Diffuse scattering happens due to light being refracted multiple times by a dielectric medium.
		    // Metals on the other hand either reflect or absorb energy, so diffuse contribution is always zero.
		    // To be energy conserving we must scale diffuse BRDF contribution based on Fresnel factor & metalness.
            float3 kd = lerp(float3(1, 1, 1) - F, float3(0, 0, 0), metalness);

		    // Lambert diffuse BRDF.
		    // We don't scale by 1/PI for lighting & material units to be more convenient.
		    // See: https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
            float3 diffuseBRDF = kd * albedo;

		    // Cook-Torrance specular microfacet BRDF.
            float3 specularBRDF = (F * D * G);
            
            if (!UseSSS)
            {
                float3 BRDF = diffuseBRDF + specularBRDF;
                directLighting = BRDF * Lradiance * NoL;
            }
            else
            {
                float invR = saturate(CurveFactor * length(fwidth(pin.Normal)) / length(fwidth(pin.WorldPosition)));
                float3 subsurfaceDiffuse = gSSSDiffuseLUTMap.Sample(gsamLinearClamp, float2(0.5 * NoL + 0.5, invR));
                //    float PH = pow(2.0 * texture(KelemenLUT,vec2(NoH, _smooth)).r, 10.0 );
                //    float F = 0.028;//fresnelReflectance( H, viewDir, 0.028 );
                //    vec3 specular = vec3(max( PH * F / dot( _h, _h ), 0 ) * _SpecularScale);
                
                float subsurfaceSpecular = pow(gSSSSpecularLUTMap.Sample(gsamLinearClamp, float2(0.5 * NoH + 0.5, roughness)), 1).r;
                float frSpec = max(subsurfaceSpecular * F / dot(Lh, Lh), 0);
                directLighting = subsurfaceDiffuse * albedo * Lradiance + 
                                    subsurfaceSpecular * Lradiance * NoL * SpecularFactor;

            }
            
            //float3 EmuL = gEmu.Sample(gsamAnisotropicClamp, float2(NoL, roughness)).rrr;
            //float3 EmuV = gEmu.Sample(gsamAnisotropicClamp, float2(NoV, roughness)).rrr;
            //
            //
            //float3 E_avg = gEavg.Sample(gsamAnisotropicClamp, float2(0, roughness)).rrr;
            
            //float3 edgetint = float3(0.827, 0.792, 0.678);
            //float3 Favg = (1.0 + F0 * 20.0) / 21.0;
            //float OneMinusEavg = 1.0 - E_avg;
            //float3 Fms = (1.0 - EmuL) * (1.0 - EmuV) * OneMinusEavg * Favg / (PI * OneMinusEavg * (1.0 - Favg * E_avg));
            
            
            // I think this effect(in my engine) is wrong
            //if(UseEmu)
               // BRDF += (Fms);

        }
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
        float3 F = F_Schlick(F0, NoV);

		// Get diffuse contribution factor (as with direct lighting).
        float3 kd = lerp(1.0 - F, 0.0, metalness);

		// Irradiance map contains exitant radiance assuming Lambertian BRDF, no need to scale by 1/PI here either.
        float3 diffuseIBL = kd * albedo * irradiance;

		// Sample pre-filtered specular reflection environment at correct mipmap level.
        uint specularTextureLevels = querySpecularTextureLevels();
        float3 specularIrradiance = gRadianceTexture.SampleLevel(gsamAnisotropicWrap, Lr, roughness * specularTextureLevels).rgb;

		// Split-sum approximation factors for Cook-Torrance specular BRDF.
        float2 specularBRDF = gLUTMap.Sample(gsamAnisotropicClamp, float2(NoV, roughness)).rg;

		// Total specular IBL contribution.
        float3 specularIBL = (F0 * specularBRDF.x + specularBRDF.y) * specularIrradiance;

		// Total ambient lighting contribution.
        ambientLighting = diffuseIBL + specularIBL;
    }

    float ambientOcclution = 1;
    if(UseSSAO)
    {
        pin.SsaoPosH /= pin.SsaoPosH.w;
        ambientOcclution += gShadowMap.Sample(gsamAnisotropicWrap, pin.SsaoPosH.xy);
        ambientOcclution = gSsaoMap.Sample(gsamAnisotropicWrap, pin.SsaoPosH.xy);
    }
    
	// Final fragment color.
    return float4(directLighting + ambientLighting * ambientOcclution, 1.0);
}
 
