#include "Common.hlsli"

// ����Ͳ�����
Texture2D ScreenTexture : register(t17); // �������Ļ����

cbuffer MaterialConstants : register(b0)
{
    float exposure;
}


struct PSInput
{
    float4 Position : SV_POSITION; // �������Ļ����
    float2 TexCoord : TEXCOORD; // �������������
};

float4 main(PSInput input) : SV_Target
{
    // ����Ļ���������ɫ
    float4 color = ScreenTexture.Sample(gsamLinearWrap, input.TexCoord);
    
    // ת��Ϊ�Ҷ�
    // float gray = dot(color.rgb, float3(0.299, 0.587, 0.114));
    // �ع�ɫ��ӳ��
    color = 1 - exp(-color * exposure);
    //color = color / (color + 1);
    color = pow(color, 1.0f / 2.2f);
    
    
    return float4(color);
}
