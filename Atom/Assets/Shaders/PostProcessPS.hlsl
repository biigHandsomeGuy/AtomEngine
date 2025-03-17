#include "Common.hlsli"


Texture2D ScreenTexture : register(t17); 
Texture2D BloomTexture : register(t18); 

cbuffer MaterialConstants : register(b0)
{
    float exposure;
}

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

float4 main(PSInput input) : SV_Target
{

    float4 sceneColor = ScreenTexture.Sample(gsamLinearWrap, input.TexCoord);
    float4 bloomColor = BloomTexture.Sample(gsamLinearWrap, input.TexCoord);
    float4 color = sceneColor;
    
    color = color / (color + 1);
    color = 1 - exp(-color * exposure);

    return float4(color);
}
