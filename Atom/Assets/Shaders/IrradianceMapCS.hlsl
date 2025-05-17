#include "Common.hlsli"
#include "PBRCommon.hlsli"

// reference: Diligent Engine

static const uint g_NumSamples = 1024;

TextureCube inputTexture : register(t0);
RWTexture2DArray<float4> outputTexture : register(u0);


// Calculate normalized sampling direction vector based on current fragment coordinates.
// This is essentially "inverse-sampling": we reconstruct what the sampling vector would be if we wanted it to "hit"
// this particular fragment in a cubemap.
float3 getSamplingVector(uint3 ThreadID)
{
	float outputWidth, outputHeight, outputDepth;
	outputTexture.GetDimensions(outputWidth, outputHeight, outputDepth);

	float2 st = ThreadID.xy / float2(outputWidth, outputHeight);
	float2 uv = 2.0 * float2(st.x, 1.0 - st.y) - 1.0;

	// Select vector based on cubemap face index.
	float3 ret;
	switch (ThreadID.z)
	{
		case 0:
			ret = float3(1.0, uv.y, -uv.x);
			break;
		case 1:
			ret = float3(-1.0, uv.y, uv.x);
			break;
		case 2:
			ret = float3(uv.x, 1.0, -uv.y);
			break;
		case 3:
			ret = float3(uv.x, -1.0, uv.y);
			break;
		case 4:
			ret = float3(uv.x, uv.y, 1.0);
			break;
		case 5:
			ret = float3(-uv.x, uv.y, -1.0);
			break;
	}
	return normalize(ret);
}

void BasisFromNormal(in float3 N, out float3 T, out float3 B)
{
	T = normalize(cross(N, abs(N.y) > 0.5 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0)));
	B = cross(T, N);
}


[numthreads(32, 32, 1)]
void main(uint3 ThreadID : SV_DispatchThreadID)
{
	float3 N = getSamplingVector(ThreadID);
	
	float3 T, B;
	BasisFromNormal(N, T, B);

    float3 irradiance = float3(0, 0, 0);
	
    for (uint i = 0u; i < g_NumSamples; i++)
    {
        float2 Xi = Hammersley2D(i, g_NumSamples);
		
		// Importance sample the hemisphere with a cosine-weighted distribution
        float3 L;
        float pdf;
        SampleDirectionCosineHemisphere(Xi, L, pdf);
		
        L = normalize(L.x * T + L.y * B + L.z * N);
		
        irradiance += inputTexture.SampleLevel(gsamLinearClamp, L, 0).rgb;

    }
    irradiance /= g_NumSamples;
	
	outputTexture[ThreadID] = float4(irradiance, 1.0);
}
