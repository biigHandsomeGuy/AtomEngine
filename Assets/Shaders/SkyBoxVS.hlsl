
cbuffer GlobalConstants : register(b1)
{
    float4x4 gView;
    float4x4 gProj;
    float4x4 gViewProj;
    float4x4 gSunShadowMatrix;
    float3 gCameraPos;
    float3 gSunPosition;

};


struct VertexIn
{
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
};


struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosL : POSITION;
};


VertexOut main(VertexIn vin)
{
    VertexOut vout;

	// Use local vertex position as cubemap lookup vector.
    vout.PosL = normalize(vin.PosL);
	// Transform to world space.
    float4 posW = float4(vin.PosL, 1);

	// Always center sky about camera.
    posW.xyz += gCameraPos;

	// Set z = w so that z/w = 1 (i.e., skydome always on far plane).
    vout.PosH = mul(gViewProj, posW).xyww;
    
    return vout;
}
