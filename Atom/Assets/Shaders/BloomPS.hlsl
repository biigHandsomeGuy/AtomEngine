#include "Common.hlsli"

Texture2D ScreenTexture : register(t17);


struct PSInput
{
    float4 Position : SV_POSITION; 
    float2 TexCoord : TEXCOORD;
};
// 高斯模糊卷积核（可以根据需要调整大小和权重）

float4 main(PSInput input) : SV_Target
{

    return float4(0,0,0,0);
    float kernel[5] = {0.227027, 0.316216, 0.070270, 0.070270, 0.316216};
    // 计算纹理的像素大小
    float2 texelSize = 1.0 / float2(512, 512);

    // 提取当前像素的颜色
    float4 color = ScreenTexture.Sample(gsamLinearWrap, input.TexCoord);
    
    // 计算亮度：使用加权平均值方法来获得亮度
    float brightness = dot(color.rgb, float3(0.2126, 0.7152, 0.0722)); // NTSC 亮度公式
    float threshold = 1.0;  // 设置亮度阈值，调节高亮部分

    // 如果亮度小于阈值，返回黑色（不参与模糊）
    if (brightness < threshold)
        return float4(0, 0, 0, 1);

    // 水平模糊
    float4 horizontalBlurredColor = float4(0.0, 0.0, 0.0, 0.0);
    for (int i = -2; i <= 2; ++i)
    {
        horizontalBlurredColor += ScreenTexture.Sample(gsamLinearWrap, input.TexCoord + float2(i, 0) * texelSize) * kernel[i + 2];
    }

    // 垂直模糊
    float4 finalBlurredColor = float4(0.0, 0.0, 0.0, 0.0);
    for (int j = -2; j <= 2; ++j)
    {
        finalBlurredColor += horizontalBlurredColor * kernel[j + 2];
    }


    return ScreenTexture.Sample(gsamLinearWrap, input.TexCoord);
}
