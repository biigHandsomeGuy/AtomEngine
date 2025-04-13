
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

float DistributionGGX(float3 N, float3 H, float roughness);
float GeometrySchlickGGX(float NdotV, float roughness);
float GeometrySmith(float3 N, float3 V, float3 L, float roughness);
float3 fresnelSchlick(float cosTheta, float3 F0);
float CalcShadowFactorPCSS(float4 shadowPosH);

float CalcShadowFactor(float4 shadowPosH);


float4 main(VertexOut pin) : SV_Target
{
    float3 albedo = 0;
    float metalness = 0;
    float roughness;
    float3 N;
    if(UseTexture)
    {
        albedo = pow(gAlbedeTexture.Sample(gsamAnisotropicWrap, pin.TexC).rgb, 2.2);
        metalness = gMetalnessTexture.Sample(gsamAnisotropicWrap, pin.TexC).r;
        roughness = gRoughnessTexture.Sample(gsamAnisotropicWrap, pin.TexC).r;
    
        // Get current fragment's normal and transform to world space.
        N = normalize(2*gNormalTexture.Sample(gsamAnisotropicWrap, pin.TexC).rgb-1);
        N = normalize(mul(pin.tangentBasis, N));
    
    }
    else
    {
        albedo = Albedo;
        metalness = Metallic;
        roughness = Roughness;
        
        N = normalize(pin.Normal);

        //return float4(N,1);
    }
    //return float4(N,1);
    // Outgoing light direction (vector from world-space fragment position to the "eye").
    float3 Lo = normalize(gCameraPos - pin.PosW);

    
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
        ambientAccess = gSsaoMap.Sample(gsamLinearWrap, pin.SsaoPosH.xy).r;
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

    }

    float3 ambientLighting = 1;
    {
        float3 irradiance = gIrradianceMap.Sample(gsamAnisotropicWrap, N).rgb;
        
        float3 F = fresnelSchlick(cosLo, F0);
        
        // Get diffuse contribution factor (as with direct lighting).
        float3 kd = lerp(1.0 - F, 0.0, metalness);
        
        // Irradiance map contains exitant radiance assuming Lambertian BRDF, no need to scale by 1/PI here either.
        float3 diffuseIBL = kd * albedo * irradiance;
             
        float3 specularIBL = 0;
        
            // Sample pre-filtered specular reflection environment at correct mipmap level.
            uint specularTextureLevels = querySpecularTextureLevels();
            float3 specularIrradiance = gSpecularMap.SampleLevel(gsamAnisotropicWrap, Lr, roughness * specularTextureLevels).rgb;

		    // Split-sum approximation factors for Cook-Torrance specular BRDF.
            float2 specularBRDF = gLUTMap.Sample(gsamAnisotropicWrap, float2(roughness, cosLo)).rg;
            
		    // Total specular IBL contribution.
            specularIBL = (1 - kd) *
            (F0 * specularBRDF.x + 1 - specularBRDF.y) * specularIrradiance;
        
        ambientLighting = diffuseIBL + specularIBL;

    }
       
    
    float3 color =ambientLighting * ambientAccess + 0;

  
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


