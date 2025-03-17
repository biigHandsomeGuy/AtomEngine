
#include "Common.hlsli"
Texture2D<float2> tex : register(t0);


struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

float4 main(VertexOut pin) : SV_Target
{
    float2 color = tex.Sample(gsamAnisotropicClamp, float2(pin.TexC.x, 1 - pin.TexC.y));
    return float4(color,0, 1);
}

