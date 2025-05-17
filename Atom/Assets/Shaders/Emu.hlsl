#include "PBRCommon.hlsli"


static const float Epsilon = 0.001; // This program needs larger eps.

static const uint g_NumSamples = 512;
static const float g_InvNumSamples = 1.0 / float(g_NumSamples);

RWTexture2D<float2> Emu : register(u0);


float GeometrySchlickGGX(float NdotV, float roughness)
{
	float a = roughness;
	float k = (a * a) / 2.0f;

	float nom = NdotV;
	float denom = NdotV * (1.0f - k) + k;

	return nom / denom;
}

float GeometrySmith(float roughness, float NoV, float NoL)
{
	float ggx2 = GeometrySchlickGGX(NoV, roughness);
	float ggx1 = GeometrySchlickGGX(NoL, roughness);

	return ggx1 * ggx2;
}

[numthreads(32, 32, 1)]
void main(uint2 ThreadID : SV_DispatchThreadID)
{
	// Get output LUT dimensions.
	float outputWidth, outputHeight;
	Emu.GetDimensions(outputWidth, outputHeight);

	// Get integration parameters.
	float NoV = (ThreadID.x) / outputWidth;
	float roughness = (ThreadID.y) / outputHeight;
    //roughness = max(0.025, roughness);
	float res = 0;
	
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
		
		
        float AlphaRoughness = roughness * roughness;
		
		float cosA = VoH;
		float F = 1 + (1.0f - 1) * pow(1 - cosA, 5.0f);
	   
        res += 4 * F * V_SmithGGXCorrelated(NoL, NoV, roughness) * VoH * NoL / NoH;
    }
	
	Emu[ThreadID] = (res * g_InvNumSamples);
}
