
cbuffer VSConstants : register(b0)
{
    float4x4 gMVP;
};


struct VertexIn
{
	float3 PosL    : POSITION;
	float2 TexC    : TEXCOORD;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
	float2 TexC    : TEXCOORD;
};

VertexOut main(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

    // Transform to homogeneous clip space.
    vout.PosH = mul(gMVP, float4(vin.PosL, 1.0f));
	
	// Output vertex attributes for interpolation across triangle.
    vout.TexC = vin.TexC;
	
    return vout;
}



