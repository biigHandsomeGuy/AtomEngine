struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

VSOutput main(uint VertexID : SV_VertexID)
{
    VSOutput output;

    float2 positions[4] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 1.0f),
        float2(1.0f, -1.0f),
        float2(1.0f, 1.0f)
    };

    float2 texCoords[4] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f)
    };

    output.Position = float4(positions[VertexID], 0.0f, 1.0f);
    output.TexCoord = texCoords[VertexID];

    return output;
}
