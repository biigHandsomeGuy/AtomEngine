RWTexture2D<float4> LUT : register(u0);

#define PI 3.14159265359

float PHBeckmann(float ndoth, float m)
{
    float alpha = acos(ndoth);
    float ta = tan(alpha);
    float val = 1.0 / (m * m * pow(ndoth, 4.0)) * exp(-(ta * ta) / (m * m));
    return val;
} // Render a screen-aligned quad to precompute a 512x512 texture.

[numthreads(32, 32, 1)]
void main(uint2 ThreadID : SV_DispatchThreadID)
{
    float outputWidth, outputHeight;
    LUT.GetDimensions(outputWidth, outputHeight);
    
    float2 uv = (float2) ThreadID / float2(outputWidth, outputHeight);
    uv.y = 1 - uv.y;
    
    float3 result = 0;
    result.r = 0.5 * pow(PHBeckmann(uv.x, uv.y), 0.1);
    
    LUT[ThreadID] = float4(result, 1.0f);
}