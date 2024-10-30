#include "Common.hlsli"


struct VertexOut
{
	float4 PosH    : SV_POSITION;
	float2 TexC    : TEXCOORD;
};

VertexOut VS(uint vertexID : SV_VertexID)
{
	VertexOut vout = (VertexOut)0.0f;
	
    float2 positions[4] =
    {
        float2(-1.0f, 1.0f), // 左上
        float2(1.0f, 1.0f), // 右上
        float2(-1.0f, -1.0f), // 左下
        float2(1.0f, -1.0f) // 右下	  
    };
    
    float2 texcoords[4] =
    {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 1.0f)
    };
    
    vout.PosH = float4(positions[vertexID], 0.0f, 1.0f);
    vout.TexC = texcoords[vertexID];
    vout.TexC.y = vout.TexC.y;
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float2 color = gLUTMap.Sample(gsamAnisotropicClamp, pin.TexC);
    return float4(color.xy,0,1);
}


