
static const float PI = 3.141592;
static const float TwoPI = 2 * PI;
static const float Epsilon = 0.00001;

static const uint NumSamples = 1024;
static const float InvNumSamples = 1.0 / float(NumSamples);

cbuffer SpecularMapFilterSettings : register(b0)
{
	// Roughness value to pre-filter for.
    float roughness;
};

TextureCube inputTexture : register(t0);
RWTexture2DArray<float4> outputTexture : register(u0);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
// Compute Van der Corput radical inverse
// See: http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float radicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

// Sample i-th point from Hammersley point set of NumSamples points total.
float2 sampleHammersley(uint i)
{
    return float2(i * InvNumSamples, radicalInverse_VdC(i));
}


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



float3 ImportanceSampleGGX(float2 xi, float roughness, float3 N)
{
    float alpha2 = roughness * roughness * roughness * roughness;
     
    float phi = 2.0f * PI * xi.x;
    float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (alpha2 - 1.0f) * xi.y));
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
     
    float3 h;
    h.x = sinTheta * cos(phi);
    h.y = sinTheta * sin(phi);
    h.z = cosTheta;
     
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangentX = normalize(cross(up, N));
    float3 tangentY = cross(N, tangentX);
     
    return (tangentX * h.x + tangentY * h.y + N * h.z);
}

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
float ndfGGX(float cosLh, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSq = alpha * alpha;

    float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
    return alphaSq / (PI * denom * denom);
}
[numthreads(32, 32, 1)]
void main(uint3 ThreadID : SV_DispatchThreadID)
{
    float inputWidth, inputHeight, inputLevels;
    inputTexture.GetDimensions(0, inputWidth, inputHeight, inputLevels);
    
    // Make sure we won't write past output when computing higher mipmap levels.
    uint outputWidth, outputHeight, outputDepth;
    outputTexture.GetDimensions(outputWidth, outputHeight, outputDepth);
    if (ThreadID.x >= outputWidth || ThreadID.y >= outputHeight)
    {
        return;
    }
    
    float3 res = (float3)0.0f;
    float totalWeight = 0.0f;
    
    float3 sampleVec = getSamplingVector(ThreadID);
    float3 normal = normalize(sampleVec);
    float3 toEye = normal;
       
    float wt = 4.0f * PI / (6.0f * inputWidth * inputHeight);
    
    //outputTexture[ThreadID] = float4(1,1,1, 1.0);
    
    for (uint i = 0; i < NumSamples; i++)
    {
        float2 xi = sampleHammersley(i);
        
        float3 halfway = ImportanceSampleGGX(xi, roughness, normal);
        float3 lightVec = 2.0f * dot(toEye, halfway) * halfway - toEye;
        
        float NdotL = saturate(dot(normal, lightVec));
        //float NdotV = saturate ( dot( normal, toEye ) ) ;
        float NdotH = saturate(dot(normal, halfway));
        float HdotV = saturate(dot(halfway, toEye));
        
        if (NdotL > 0)
        {           
            float D = DistributionGGX(normal, halfway, roughness);
            float pdf = (D * NdotH / (4 * HdotV));
            pdf = ndfGGX(NdotH, roughness);
            
            float ws = 1.0f / (NumSamples * pdf);
             
            //int mipLevel = roughness == 0.0f ? 0.0f : 0.5f * log2(ws / wt);
            float mipLevel = max(0.5 * log2(ws / wt) + 1.0, 0.0);
         
            res += inputTexture.SampleLevel(gsamLinearWrap, lightVec, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    
    res /= totalWeight;
            
    outputTexture[ThreadID] = float4(res, 1.0);
}