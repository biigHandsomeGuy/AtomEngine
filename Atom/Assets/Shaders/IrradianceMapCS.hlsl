#include "PBR_Common.hlsli"


static const uint NumSamples = 512;
static const float InvNumSamples = 1.0 / float(NumSamples);

TextureCube inputTexture : register(t0);
RWTexture2DArray<float4> outputTexture : register(u0);

SamplerState defaultSampler : register(s0);


float3 getSamplingVector(uint3 ThreadID)
{
	float outputWidth, outputHeight, outputDepth;
	outputTexture.GetDimensions(outputWidth, outputHeight, outputDepth);

    float2 st = ThreadID.xy/float2(outputWidth, outputHeight);
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

// Compute orthonormal basis for converting from tanget/shading space to world space.
void computeBasisVectors(const float3 N, out float3 S, out float3 T)
{
	// Branchless select non-degenerate T.
	T = cross(N, float3(0.0, 1.0, 0.0));
	T = lerp(cross(N, float3(1.0, 0.0, 0.0)), T, step(0.00001, dot(T, T)));

	T = normalize(T);
	S = normalize(cross(N, T));
}

// Convert point from tangent/shading space to world space.
float3 tangentToWorld(const float3 v, const float3 N, const float3 S, const float3 T)
{
	return S * v.x + T * v.y + N * v.z;
}

[numthreads(32, 32, 1)]
void main(uint3 ThreadID : SV_DispatchThreadID)
{
	float3 N = getSamplingVector(ThreadID);
	
	float3 B, T;
	computeBasisVectors(N, B, T);

	// Monte Carlo integration of hemispherical irradiance.
	// As a small optimization this also includes Lambertian BRDF assuming perfectly white surface (albedo of 1.0)
	// so we don't need to normalize in PBR fragment shader (so technically it encodes exitant radiance rather than irradiance).
	float3 irradiance = 0.0;
	for(uint i=0; i<NumSamples; ++i) {

		float2 Xi = Hammersley2D(i,NumSamples);
		float3 L;
        float  pdf;
        SampleDirectionCosineHemisphere(Xi, L, pdf);
		L = normalize(L.x * T + L.y * B + L.z * N);
        float OmegaP = ComputeCubeMapPixelSolidAngle(256, 256);
		float OmegaS = 1.0 / (float(NumSamples) * pdf);
		float MipBias = 1.0;
        float MipLevel = clamp(0.5 * log2(OmegaS / max(OmegaP, 1e-10)) + MipBias, 0.0, 1 - 1.0);
		// PIs here cancel out because of division by pdf.
		irradiance += inputTexture.SampleLevel(defaultSampler, L, 0).rgb;
	}
	irradiance /= float(NumSamples);

	outputTexture[ThreadID] = float4(irradiance, 1.0);
}