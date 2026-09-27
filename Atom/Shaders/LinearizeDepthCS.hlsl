Texture2D<float> gDepthBuffer : register(t0);
RWTexture2D<float> gLinearDepth : register(u0);

cbuffer SsaoCB : register(b0)
{
    float4x4 gProj;
    float4x4 gInvProj;
    float4 gSampleKernel[16];
    float4 gInvSize;
    float4 gAoParams;
};

cbuffer RootConstants : register(b1)
{
    uint gPassMode;
};

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint outW, outH;
    gLinearDepth.GetDimensions(outW, outH);
    if (DTid.x >= outW || DTid.y >= outH)
    {
        return;
    }

    float depth = gDepthBuffer.Load(int3(DTid.xy, 0));
    float2 uv = (float2(DTid.xy) + 0.5f) / float2(outW, outH);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 clip = float4(ndc, depth, 1.0f);
    float4 view = mul(gInvProj, clip);

    gLinearDepth[DTid.xy] = abs(view.z / max(abs(view.w), 1.0e-6f));
}
