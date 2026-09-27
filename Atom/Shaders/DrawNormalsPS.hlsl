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
    float2 gJitter;
    float2 gPad2;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 NormalW : NORMAL;
};

struct PsOut
{
    float4 Color : SV_Target0;
};

PsOut main(VertexOut pin)
{
    PsOut outVal;
	// Interpolating normal can unnormalize it, so renormalize it.
    pin.NormalW = normalize(pin.NormalW);
	
    // Write normal in view space coordinates
    float3 normalV = mul((float3x3) gView, pin.NormalW);
    outVal.Color = float4(normalV, 1.0f);
    return outVal;
}
