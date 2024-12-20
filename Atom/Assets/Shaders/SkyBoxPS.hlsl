#include "Common.hlsli"

TextureCube gCubeMap : register(t13);

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosL : POSITION;
};

cbuffer MaterialConstants : register(b0)
{
    float EnvMipMap;
}

float4 main(VertexOut pin) : SV_TARGET
{	   
    float3 color = gCubeMap.SampleLevel(gsamAnisotropicWrap, pin.PosL, EnvMipMap).rgb;
    
    return float4(color, 1.0f);
}

