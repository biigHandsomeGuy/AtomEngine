#include "PBRCommon.hlsli"


static const float Epsilon = 0.001; // This program needs larger eps.

static const uint g_NumSamples = 512;
static const float g_InvNumSamples = 1.0 / float(g_NumSamples);

RWTexture2D<float2> LUT : register(u0);
// Importance sample GGX normal distribution function for a fixed roughness value.
// This returns normalized half-vector between Li & Lo.
// For derivation see: http://blog.tobias-franke.eu/2014/03/30/notes_on_importance_sampling.html
float3 sampleGGX(float u1, float u2, float roughness)
{
	float alpha = roughness * roughness;

	float cosTheta = sqrt((1.0 - u2) / (1.0 + (alpha * alpha - 1.0) * u2));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta); // Trig. identity
	float phi = 2 * PI * u1;

	// Convert to Cartesian upon return.
	return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

// Single term for separable Schlick-GGX below.
float gaSchlickG1(float cosTheta, float k)
{
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

// Schlick-GGX approximation of geometric attenuation function using Smith's method (IBL version).
float gaSchlickGGX_IBL(float cosLi, float cosLo, float roughness)
{
	float r = roughness;
	float k = (r * r) / 2.0; // Epic suggests using this roughness remapping for IBL lighting.
	return gaSchlickG1(cosLi, k) * gaSchlickG1(cosLo, k);
}

[numthreads(32, 32, 1)]
void main(uint2 ThreadID : SV_DispatchThreadID)
{
	// Get output LUT dimensions.
	float outputWidth, outputHeight;
	LUT.GetDimensions(outputWidth, outputHeight);

	// Get integration parameters.
    float NoV = (ThreadID.x) / outputWidth;
	float roughness = (ThreadID.y) / outputHeight;

	float3 V;
	V.x = sqrt(1.0f - NoV * NoV); // sin
	V.y = 0;
	V.z = NoV;
	const float3 N = float3(0.0, 0.0, 1.0);

	float DFG1 = 0.0f, DFG2 = 0.0f;
	for (uint i = 0u; i < g_NumSamples; i++)
	{
        float2 Xi = Hammersley2D(i, g_NumSamples);
        float3 H = ImportanceSampleGGX(Xi, roughness, N);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
		
        float NoL = saturate(L.z);
        float NoH = saturate(H.z);
        float VoH = saturate(dot(V, H));
		
		if(NoL > 0)
        {
            float AlphaRoughness = roughness * roughness;
			
            float G_Vis = 4.0f * V_SmithGGXCorrelated(NoL, NoV, AlphaRoughness) * VoH * NoL / NoH;
            float Fc = pow(1.0f - VoH, 5.0f);
			
            DFG1 += G_Vis * (1 - Fc);
            DFG2 += G_Vis * Fc;

        }

    }
	
	LUT[ThreadID] = float2(DFG1, DFG2) * g_InvNumSamples;
}
