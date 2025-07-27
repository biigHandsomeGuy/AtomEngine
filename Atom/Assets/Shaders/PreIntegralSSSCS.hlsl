RWTexture2D<float4> LUT : register(u0);

#define PI 3.14159265359

float3 Gaussian(float delta, float x)
{
    return 1 / (sqrt(2 * PI) * delta) * exp((-x * x) / (2 * delta * delta));
}

float3 NvidiaDiffusionProfile(float r)
{
    return Gaussian(0.0064, r) * float3(0.233, 0.455, 0.649)
        + Gaussian(0.0484, r) * float3(0.100, 0.336, 0.344)
        + Gaussian(0.187, r) * float3(0.118, 0.198, 0.0)
        + Gaussian(0.567, r) * float3(0.113, 0.007, 0.007)
        + Gaussian(1.99, r) * float3(0.358, 0.004, 0.0)
        + Gaussian(7.41, r) * float3(0.078, 0.0, 0.0);
}

[numthreads(32, 32, 1)]
void main(uint2 ThreadID : SV_DispatchThreadID)
{
    float outputWidth, outputHeight;
    LUT.GetDimensions(outputWidth, outputHeight);
    
    float2 uv = (float2) ThreadID / float2(outputWidth, outputHeight);
    uv.y = 1 - uv.y;
    
    float NoL = uv.x;
    float R = 1 / max(0.0001, uv.y);
    float theta = acos(NoL * 2.0 - 1.0);
    float3 scatteringFactor = float3(0.0, 0.0, 0.0);
    float3 normalizationFactor = float3(0.0, 0.0, 0.0);
    
    for (float x = -PI; x < PI; x += PI*0.001)
    {
        float distance = 2 * R * sin(x / 2);
        float3 weight = NvidiaDiffusionProfile(distance);
        //weight = float3(0.3, 0.9, 0.5);
        scatteringFactor += saturate(cos(x + theta)).xxx * weight;
        normalizationFactor += weight;
    }
    float3 result = scatteringFactor / normalizationFactor;
    //result = pow(result, 1 / 2.2);
    LUT[ThreadID] = float4(result, 1.0f);

}