#include "PBRCommon.hlsli"


static const float Epsilon = 0.001; // This program needs larger eps.

static const uint g_NumSamples = 512;
static const float g_InvNumSamples = 1.0 / float(g_NumSamples);

RWTexture2D<float2> LUT : register(u0);




[numthreads(32, 32, 1)]
void main(uint2 ThreadID : SV_DispatchThreadID)
{
	// Get output LUT dimensions.
	float outputWidth, outputHeight;
	LUT.GetDimensions(outputWidth, outputHeight);

	// Get integration parameters.
    float NoV = (ThreadID.x) / outputWidth;
	float roughness = (ThreadID.y) / outputHeight;
    //roughness = max(0.0025, roughness);
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
