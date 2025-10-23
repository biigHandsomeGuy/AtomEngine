cbuffer VSConstant : register(b0)
{
    float4x4 gModel;
    float4x4 gNormalMatrix;
}

cbuffer GlobalConstants : register(b1)
{
    float4x4 gView;
    float4x4 gProj;
    float4x4 gViewProj;
    float4x4 gSunShadowMatrix;
    float3 gCameraPos;
    float pad;
    float3 gSunPosition;

};

struct VertexIn
{
	float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
};

struct VertexOut
{
	float4 PosH     : SV_POSITION;
    float3 NormalW  : NORMAL;
};

VertexOut main(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

    // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
    // vout.NormalW = mul((float3x3)gWorld, vin.NormalL);
    vout.NormalW = mul((float3x3)gNormalMatrix, vin.NormalL);
    
    // Transform to homogeneous clip space.

    vout.PosH = mul(gViewProj, mul(gModel, float4(vin.PosL, 1.0f)));
	
    return vout;
}


