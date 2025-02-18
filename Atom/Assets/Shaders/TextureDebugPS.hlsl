
#include "Common.hlsli"
Texture2D<float2> gLUTMap : register(t16);


struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

float4 main(VertexOut pin) : SV_Target
{
    float2 color = gLUTMap.Sample(gsamAnisotropicClamp, pin.TexC);
    return float4(color,0, 1);
}

