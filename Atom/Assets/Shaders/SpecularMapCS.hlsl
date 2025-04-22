#include "Common.hlsli"
#include "PBRCommon.hlsli"

// reference
// Diligent Engine

static const uint g_NumSamples = 1024;

cbuffer SpecularMapFilterSettings : register(b0)
{
	float roughness;
};

TextureCube inputTexture : register(t0);
RWTexture2DArray<float4> outputTexture : register(u0);


float3 getSamplingVector(uint3 ThreadID)
{
	float outputWidth, outputHeight, outputDepth;
	outputTexture.GetDimensions(outputWidth, outputHeight, outputDepth);

	float2 st = (ThreadID.xy + 0.5) / float2(outputWidth, outputHeight);
	float2 uv = 2.0 * float2(st.x, 1.0-st.y) - 1.0;

	// Select vector based on cubemap face index.
	float3 ret;
	switch(ThreadID.z)
	{
	case 0: ret = float3(1.0,  uv.y, -uv.x); break;
	case 1: ret = float3(-1.0, uv.y,  uv.x); break;
	case 2: ret = float3(uv.x, 1.0, -uv.y); break;
	case 3: ret = float3(uv.x, -1.0, uv.y); break;
	case 4: ret = float3(uv.x, uv.y, 1.0); break;
	case 5: ret = float3(-uv.x, uv.y, -1.0); break;
	}
	return normalize(ret);
}

[numthreads(32, 32, 1)]
void main(uint3 ThreadID : SV_DispatchThreadID)
{
	// Make sure we won't write past output when computing higher mipmap levels.
	uint outputWidth, outputHeight, outputDepth;
	outputTexture.GetDimensions(outputWidth, outputHeight, outputDepth);
	if(ThreadID.x >= outputWidth || ThreadID.y >= outputHeight) {
		return;
	}
	
	// Get input cubemap dimensions at zero mipmap level.
	float inputWidth, inputHeight, inputLevels;
	inputTexture.GetDimensions(0, inputWidth, inputHeight, inputLevels);

	// Solid angle associated with a single cubemap texel at zero mipmap level.
	// This will come in handy for importance sampling below.
	float wt = 4.0 * PI / (6 * inputWidth * inputHeight);
	
	// Approximation: Assume zero viewing angle (isotropic reflections).
	float3 R = getSamplingVector(ThreadID);
	float3 N = R;
	float3 V = R;
	float3 PrefilteredColor = float3(0.0, 0.0, 0.0);
	float TotalWeight = 0.0;
	
	// Convolve environment map using GGX NDF importance sampling.
	// Weight by cosine term since Epic claims it generally improves quality.
	for(uint i = 0; i < g_NumSamples; ++i) 
	{
		float2 Xi = Hammersley2D( i, g_NumSamples);
		float3 H  = ImportanceSampleGGX( Xi, roughness, N );
		float3 L  = 2.0 * dot(V, H) * H - V;

		
		
		float NoL = saturate(dot(N, L));
		float VoH = saturate(dot(V, H));
		float NoH = saturate(dot(N, H));
		
		if(NoL > 0.0 && VoH > 0.0) 
		{
			// Probability Distribution Function
			float AlphaRoughness = roughness * roughness;
			float pdf = max(SmithGGXSampleDirectionPDF(V, N, L, AlphaRoughness), 0.0001);
		   
			// Solid angle associated with this sample.
			float ws = 1.0 / (g_NumSamples * pdf);

			// Mip level to sample from.
			float mipLevel = max(0.5 * log2(ws / wt) + 2, 0.0);

			PrefilteredColor  += inputTexture.SampleLevel(gsamLinearClamp, L, mipLevel).rgb * NoL;
			TotalWeight += NoL;
		}
	}
	PrefilteredColor /= TotalWeight;

	outputTexture[ThreadID] = float4(PrefilteredColor, 1.0);
}

