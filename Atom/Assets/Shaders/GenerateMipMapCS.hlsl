cbuffer CB0 : register(b0)
{
    int size;
};

cbuffer CB : register(b1)
{
    int mipmap;
};

TextureCube<float4> SrcTexture : register(t0);
RWTexture2DArray<float4> DstTexture : register(u0);
SamplerState BilinearClamp : register(s3);



float3 getSamplingVector(uint3 ThreadID)
{
    float2 texcoord = (ThreadID.xy + 0.5) / float2(size, size);
    texcoord.y = 1 - texcoord.y;
    texcoord = texcoord * 2.0 - 1.0; // 将 [0,1] 转换为 [-1,1]
    

    // 根据面索引返回采样向量，标准的6面 cubemap
    float3 ret;
    switch (ThreadID.z)
    {
        case 0:
            ret = float3(1.0, texcoord.y, -texcoord.x);
            break; // +X
        case 1:
            ret = float3(-1.0, texcoord.y, texcoord.x);
            break; // -X
        case 2:
            ret = float3(texcoord.x, 1.0, -texcoord.y);
            break; // +Y
        case 3:
            ret = float3(texcoord.x, -1.0, texcoord.y);
            break; // -Y
        case 4:
            ret = float3(texcoord.x, texcoord.y, 1.0);
            break; // +Z
        case 5:
            ret = float3(-texcoord.x, texcoord.y, -1.0);
            break; // -Z
    }
    return normalize(ret);
}


[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    
    float3 lo = getSamplingVector(DTid);
    

	//The samplers linear interpolation will mix the four pixel values to the new pixels color
    float4 color = SrcTexture.SampleLevel(BilinearClamp, lo, mipmap);
	//Write the final color into the destination texture.
    
    //DstTexture[DTid] = float4(DTid.xy / size, 0.5, 1);
    DstTexture[DTid] = color;
}