#ifndef PBR_COMMON_HLSLI
#define PBR_COMMON_HLSLI


// reference
// https://zhuanlan.zhihu.com/p/53086060
// Diligent Engine
// Unreal Engine

#ifndef PI
#define PI 3.1415926536
#endif


float3 Diffuse_Lambert(float3 DiffuseColor)
{
    return DiffuseColor * (1 / PI);
}


// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX(float a2, float NoH)
{
    float d = (NoH * a2 - NoH) * NoH + 1; // 2 mad
    return a2 / (PI * d * d); // 4 mul, 1 rcp
}

// Schlick [1994]
float F_Schlick(float3 F0, float cosTheta)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float G_Smith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}


// Returns a random cosine-weighted direction on the hemisphere around z = 1.
void SampleDirectionCosineHemisphere(in float2 UV, // Normal random variables
                                     out float3 Dir, // Direction
                                     out float Prob // Probability of the generated direction
                                     )
{
    Dir.x = cos(2.0 * PI * UV.x) * sqrt(1.0 - UV.y);
    Dir.y = sin(2.0 * PI * UV.x) * sqrt(1.0 - UV.y);
    Dir.z = sqrt(UV.y);

    // Avoid zero probability
    Prob = max(Dir.z, 1e-6) / PI;
}

float ComputeCubeMapPixelSolidAngle(float Width, float Height)
{
    return 4.0 * PI / (6.0 * Width * Height);
}

// Returns a random cosine-weighted direction on the hemisphere around N.
void SampleDirectionCosineHemisphere(in float3 N, // Normal
                                     in float2 UV, // Normal random variables
                                     out float3 Dir, // Direction
                                     out float Prob // Probability of the generated direction
                                     )
{
    float3 T = normalize(cross(N, abs(N.y) > 0.5 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0)));
    float3 B = cross(T, N);
    SampleDirectionCosineHemisphere(UV, Dir, Prob);
    Dir = normalize(Dir.x * T + Dir.y * B + Dir.z * N);
}

float2 Hammersley2D(uint i, uint N)
{
    // Radical inverse based on http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
    uint bits = reversebits(i);
    float rdi = float(bits) * 2.3283064365386963e-10;
    return float2(float(i) / float(N), rdi);
}

// Based on https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
float3 ImportanceSampleGGX(float2 Xi, float Roughness, float3 N)
{
    float a = Roughness * Roughness;
    
    float Phi = 2 * PI * Xi.x;
    float CosTheta = sqrt((1 - Xi.y) / (1 + (a * a - 1) * Xi.y));
    float SinTheta = sqrt(1 - CosTheta * CosTheta);
    
    float3 H;
    H.x = SinTheta * cos(Phi);
    H.y = SinTheta * sin(Phi);
    H.z = CosTheta;
    
    float3 UpVector = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 TangentX = normalize(cross(UpVector, N));
    float3 TangentY = cross(N, TangentX);
    // Tangent to world space
    return TangentX * H.x + TangentY * H.y + N * H.z;
}

// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games, Equation 3.
float NormalDistribution_GGX(float NdotH, float AlphaRoughness)
{
    // "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz - eq. (1)
    // https://jcgt.org/published/0007/04/01/

    // Make sure we reasonably handle AlphaRoughness == 0
    // (which corresponds to delta function)
    AlphaRoughness = max(AlphaRoughness, 1e-3);

    float a2 = AlphaRoughness * AlphaRoughness;
    float nh2 = NdotH * NdotH;
    float f = nh2 * a2 + (1.0 - nh2);
    return a2 / max(PI * f * f, 1e-9);
}
// Smith GGX masking function G1
// [1] "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz - eq. (2)
// https://jcgt.org/published/0007/04/01/
float SmithGGXMasking(float NdotV, float AlphaRoughness)
{
    float a2 = AlphaRoughness * AlphaRoughness;

    // In [1], eq. (2) is defined for the tangent-space view direction V:
    //
    //                                        1
    //      G1(V) = -----------------------------------------------------------
    //                                    {      (ax*V.x)^2 + (ay*V.y)^2)  }
    //               1 + 0.5 * ( -1 + sqrt{ 1 + -------------------------- } )
    //                                    {              V.z^2             }
    //
    // Note that [1] uses notation N for the micronormal, but in our case N is the macronormal,
    // while micronormal is H (aka the halfway vector).
    //
    // After multiplying both nominator and denominator by 2*V.z and given that in our
    // case ax = ay = a, we get:
    //
    //                                2 * V.z                                        2 * V.z
    //      G1(V) = ------------------------------------------- =  ----------------------------------------
    //               V.z + sqrt{ V.z^2 + a2 * (V.x^2 + V.y^2) }     V.z + sqrt{ V.z^2 + a2 * (1 - V.z^2) }
    //
    // Since V.z = NdotV, we finally get:

    float Denom = NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
    return 2.0 * max(NdotV, 0.0) / max(Denom, 1e-6);
}



// Returns the probability of sampling direction L for the view direction V and normal N
// using the visible normals distribution.
// [1] "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz
// https://jcgt.org/published/0007/04/01/
float SmithGGXSampleDirectionPDF(float3 V, float3 N, float3 L, float AlphaRoughness)
{
    // Micronormal is the halfway vector
    float3 H = normalize(V + L);

    float NdotH = dot(H, N);
    float NdotV = dot(N, V);
    float NdotL = dot(N, L);
    //float VdotH = dot(V, H);
    if (NdotH > 0.0 && NdotV > 0.0 && NdotL > 0.0)
    {
        // Note that [1] uses notation N for the micronormal, but in our case N is the macronormal,
        // while micronormal is H (aka the halfway vector).
        float NDF = NormalDistribution_GGX(NdotH, AlphaRoughness); // (1) - D(N)
        float G1 = SmithGGXMasking(NdotV, AlphaRoughness); // (2) - G1(V)

        float VNDF = G1 /* * VdotH */ * NDF / NdotV; // (3) - Dv(N)
        return VNDF / (4.0 /* * VdotH */); // (17) - VdotH cancels out
    }
    else
    {
        return 0.0;
    }
}


#endif