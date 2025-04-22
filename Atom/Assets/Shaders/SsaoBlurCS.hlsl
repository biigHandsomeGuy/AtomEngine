
Texture2D gNormalMap : register(t0);
Texture2D gDepthMap : register(t1);
Texture2D gInputMap : register(t2);

SamplerState gsamPointClamp : register(s0);
SamplerState gsamLinearClamp : register(s1);
SamplerState gsamDepthMap : register(s2);
SamplerState gsamLinearWrap : register(s3);
RWTexture2D<float4> gOutputMap : register(u0);

cbuffer cbSsao : register(b0)
{
	float4x4 gProj;
	float4x4 gInvProj;
	float4x4 gProjTex;
	float4 gOffsetVectors[14];

	// For SsaoBlur.hlsl
	float4 gBlurWeights[3];

	float2 gInvRenderTargetSize;

	// Coordinates given in view space.
	float gOcclusionRadius;
	float gOcclusionFadeStart;
	float gOcclusionFadeEnd;
	float gSurfaceEpsilon;
};

static const int gBlurRadius = 5;

cbuffer cbRootConstants : register(b1)
{
	bool gHorizontalBlur;
};


float NdcDepthToViewDepth(float z_ndc)
{
	return gProj[3][2] / (z_ndc - gProj[2][2]);
}

[numthreads(32, 32, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{  
	float2 texC = (DTid.xy + 0.5f) * gInvRenderTargetSize;

	// unpack into float array.
	float blurWeights[12] =
	{
		gBlurWeights[0].x, gBlurWeights[0].y, gBlurWeights[0].z, gBlurWeights[0].w,
		gBlurWeights[1].x, gBlurWeights[1].y, gBlurWeights[1].z, gBlurWeights[1].w,
		gBlurWeights[2].x, gBlurWeights[2].y, gBlurWeights[2].z, gBlurWeights[2].w,
	};

	float4 color = blurWeights[gBlurRadius] * gInputMap.SampleLevel(gsamPointClamp, texC, 0);
	float totalWeight = blurWeights[gBlurRadius];

	float3 centerNormal = gNormalMap.SampleLevel(gsamPointClamp, texC, 0).xyz;
	float centerDepth = NdcDepthToViewDepth(gDepthMap.SampleLevel(gsamDepthMap, texC, 0).r);

	float2 texOffset;
	if (gHorizontalBlur)
	{
		texOffset = float2(gInvRenderTargetSize.x, 0.0f);
	}
	else
	{
		texOffset = float2(0.0f, gInvRenderTargetSize.y);
	}
	
	for (float i = -gBlurRadius; i <= gBlurRadius; ++i)
	{
		// We already added in the center weight.
		if (i == 0)
			continue;

		float2 tex = texC + i * texOffset;

		float3 neighborNormal = gNormalMap.SampleLevel(gsamPointClamp, tex, 0.0f).xyz;
		float neighborDepth = NdcDepthToViewDepth(
			gDepthMap.SampleLevel(gsamDepthMap, tex, 0.0f).r);

		//
		// If the center value and neighbor values differ too much (either in 
		// normal or depth), then we assume we are sampling across a discontinuity.
		// We discard such samples from the blur.
		//
	
		if (dot(neighborNormal, centerNormal) >= 0.8f &&
			abs(neighborDepth - centerDepth) <= 0.2f)
		{
			float weight = blurWeights[i + gBlurRadius];

			// Add neighbor pixel to blur.
			color += weight * gInputMap.SampleLevel(
				gsamPointClamp, tex, 0.0);
		
			totalWeight += weight;
		}
	}

	gOutputMap[DTid.xy] = color / totalWeight;
}