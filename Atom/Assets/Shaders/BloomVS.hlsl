// 顶点着色器
struct VSOutput
{
    float4 Position : SV_POSITION; // 输出到屏幕的坐标
    float2 TexCoord : TEXCOORD; // 输出到像素着色器的纹理坐标
};

VSOutput main(uint VertexID : SV_VertexID)
{
    VSOutput output;

    // 定义屏幕四边形的顶点位置和纹理坐标
    float2 positions[4] =
    {
        float2(-1.0f, -1.0f), // 左下角
        float2(-1.0f, 1.0f), // 左上角
        float2(1.0f, -1.0f), // 右下角
        float2(1.0f, 1.0f) // 右上角
    };

    float2 texCoords[4] =
    {
        float2(0.0f, 1.0f), // 对应左下角
        float2(0.0f, 0.0f), // 对应左上角
        float2(1.0f, 1.0f), // 对应右下角
        float2(1.0f, 0.0f) // 对应右上角
    };

    // 根据顶点 ID 选择顶点
    output.Position = float4(positions[VertexID], 0.0f, 1.0f); // 齐次坐标
    output.TexCoord = texCoords[VertexID]; // 纹理坐标

    return output;
}
