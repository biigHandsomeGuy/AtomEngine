// ������ɫ��
struct VSOutput
{
    float4 Position : SV_POSITION; // �������Ļ������
    float2 TexCoord : TEXCOORD; // �����������ɫ������������
};

VSOutput main(uint VertexID : SV_VertexID)
{
    VSOutput output;

    // ������Ļ�ı��εĶ���λ�ú���������
    float2 positions[4] =
    {
        float2(-1.0f, -1.0f), // ���½�
        float2(-1.0f, 1.0f), // ���Ͻ�
        float2(1.0f, -1.0f), // ���½�
        float2(1.0f, 1.0f) // ���Ͻ�
    };

    float2 texCoords[4] =
    {
        float2(0.0f, 1.0f), // ��Ӧ���½�
        float2(0.0f, 0.0f), // ��Ӧ���Ͻ�
        float2(1.0f, 1.0f), // ��Ӧ���½�
        float2(1.0f, 0.0f) // ��Ӧ���Ͻ�
    };

    // ���ݶ��� ID ѡ�񶥵�
    output.Position = float4(positions[VertexID], 0.0f, 1.0f); // �������
    output.TexCoord = texCoords[VertexID]; // ��������

    return output;
}
