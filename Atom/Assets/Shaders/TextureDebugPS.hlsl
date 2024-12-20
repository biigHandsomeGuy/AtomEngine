
#include "Common.hlsli"
Texture2D<float2> gLUTMap : register(t16);


struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

float4 main(VertexOut pin) : SV_Target
{
    float color = gSsaoMap.Sample(gsamAnisotropicClamp, pin.TexC).x;
    return float4(color.xxx, 1);
}

