
struct VertexOut
{
	float4 PosH    : SV_POSITION;
	float2 TexC    : TEXCOORD;
};

VertexOut main(uint vertexID : SV_VertexID)
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

