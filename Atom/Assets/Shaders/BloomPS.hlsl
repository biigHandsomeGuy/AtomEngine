#include "Common.hlsli"

Texture2D ScreenTexture : register(t17);


struct PSInput
{
    float4 Position : SV_POSITION; 
    float2 TexCoord : TEXCOORD;
};
// ��˹ģ������ˣ����Ը�����Ҫ������С��Ȩ�أ�

float4 main(PSInput input) : SV_Target
{

    return float4(0,0,0,0);
    float kernel[5] = {0.227027, 0.316216, 0.070270, 0.070270, 0.316216};
    // ������������ش�С
    float2 texelSize = 1.0 / float2(512, 512);

    // ��ȡ��ǰ���ص���ɫ
    float4 color = ScreenTexture.Sample(gsamLinearWrap, input.TexCoord);
    
    // �������ȣ�ʹ�ü�Ȩƽ��ֵ�������������
    float brightness = dot(color.rgb, float3(0.2126, 0.7152, 0.0722)); // NTSC ���ȹ�ʽ
    float threshold = 1.0;  // ����������ֵ�����ڸ�������

    // �������С����ֵ�����غ�ɫ��������ģ����
    if (brightness < threshold)
        return float4(0, 0, 0, 1);

    // ˮƽģ��
    float4 horizontalBlurredColor = float4(0.0, 0.0, 0.0, 0.0);
    for (int i = -2; i <= 2; ++i)
    {
        horizontalBlurredColor += ScreenTexture.Sample(gsamLinearWrap, input.TexCoord + float2(i, 0) * texelSize) * kernel[i + 2];
    }

    // ��ֱģ��
    float4 finalBlurredColor = float4(0.0, 0.0, 0.0, 0.0);
    for (int j = -2; j <= 2; ++j)
    {
        finalBlurredColor += horizontalBlurredColor * kernel[j + 2];
    }


    return ScreenTexture.Sample(gsamLinearWrap, input.TexCoord);
}
