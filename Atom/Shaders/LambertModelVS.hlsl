


cbuffer MeshConstants : register(b0)
{
    float4x4 gWorldMatrix;
    float4x4 gNormalMatrix;
    float4x4 gViewProjTex;
};

cbuffer GlobalConstants : register(b1)
{
    float4x4 gView;
    float4x4 gProj;
    float4x4 gViewProj;
    float4x4 gSunShadowMatrix;
    float3 gCameraPos;
    float pad;
    float3 gSunPosition;
    float pad1;
    
};

struct VertexIn
{
	float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
	float3 Tangent : TANGENT;
	float3 BiTangent : BITANGENT;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
    float4 ShadowPosH : POSITION0;
    float4 SsaoPosH   : POSITION1;
    float3 PosW    : POSITION2;
	float2 TexC    : TEXCOORD;
    float3x3 tangentBasis : TAASIC;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
};

VertexOut main(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;
    
    // Transform to world space.
    float4 posW = mul(gWorldMatrix, float4(vin.PosL, 1.0f));
    vout.PosW = posW.xyz;

    float3x3 TBN = float3x3(vin.Tangent, vin.BiTangent, vin.NormalL);       
    vout.tangentBasis = mul((float3x3) gWorldMatrix, transpose(TBN));
    
    // Transform to homogeneous clip space.
    //vout.PosH = mul(gViewProj, posW);
    vout.PosH = mul(gProj, mul(gView, posW));
    // Generate projective tex-coords to project SSAO map onto scene.
    vout.SsaoPosH = mul(gViewProjTex, posW);
	
	// Output vertex attributes for interpolation across triangle.
    vout.TexC = float2(vin.TexC.x, 1 - vin.TexC.y);

    vout.Normal = vin.NormalL;
    vout.Tangent = vin.Tangent;
    // Generate projective tex-coords to project shadow map onto scene.
    vout.ShadowPosH = mul(gSunShadowMatrix, posW);
	
    return vout;
}



