Texture2D<float> gLinearDepthFull : register(t0);
RWTexture2D<float> gLinearDepthHalf : register(u0);

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
    gLinearDepthHalf.GetDimensions(outW, outH);
    if (DTid.x >= outW || DTid.y >= outH)
    {
        return;
    }

    uint fullW, fullH;
    gLinearDepthFull.GetDimensions(fullW, fullH);
    int2 edge = int2(fullW, fullH) - 1;
    int2 basePixel = int2(DTid.xy) * 2;
    float z0 = gLinearDepthFull.Load(int3(min(basePixel, edge), 0));
    float z1 = gLinearDepthFull.Load(int3(min(basePixel + int2(1, 0), edge), 0));
    float z2 = gLinearDepthFull.Load(int3(min(basePixel + int2(0, 1), edge), 0));
    float z3 = gLinearDepthFull.Load(int3(min(basePixel + int2(1, 1), edge), 0));

    gLinearDepthHalf[DTid.xy] = min(min(z0, z1), min(z2, z3));
}
