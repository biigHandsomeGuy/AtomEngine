#include "Common.hlsli"


Texture2D ScreenTexture : register(t17); 
Texture2D BloomTexture : register(t18); 
cbuffer MaterialConstants : register(b0)
{
    float exposure;
    bool isRenderingLuminance;
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
    
    //color = color / (color + 1);
    color = 1 - exp(-color * exposure);   
    
    color = pow(color, 1 / 2.2);
    if (isRenderingLuminance)
    {
        float luminance = color.r * 0.299 + color.g * 0.587 + color.b * 0.114;
        return float4(luminance, luminance, luminance, 1);
    }
    else
    {
        return color;

    }
    
}
