#include "Common.hlsli"

// 纹理和采样器
Texture2D ScreenTexture : register(t17); // 输入的屏幕纹理

cbuffer MaterialConstants : register(b0)
{
    float exposure;
}


struct PSInput
{
    float4 Position : SV_POSITION; // 输入的屏幕坐标
    float2 TexCoord : TEXCOORD; // 输入的纹理坐标
};

float4 main(PSInput input) : SV_Target
{
    // 从屏幕纹理采样颜色
    float4 color = ScreenTexture.Sample(gsamLinearWrap, input.TexCoord);
    
    // 转换为灰度
    // float gray = dot(color.rgb, float3(0.299, 0.587, 0.114));
    // 曝光色调映射
    color = 1 - exp(-color * exposure);
    //color = color / (color + 1);
    color = pow(color, 1.0f / 2.2f);
    
    
    return float4(color);
}
