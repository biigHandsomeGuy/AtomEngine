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
#ifdef ATOM_PBR_MOTION
    float2 gInvScreenSize;
#else
    float2 gPad2;
#endif
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 NormalW : NORMAL;
#ifdef ATOM_PBR_MOTION
    float4 PrevClip : TEXCOORD0;
    nointerpolation uint MotionValid : TEXCOORD1;
#endif
};

struct PsOut
{
    float4 Color : SV_Target0;
#ifdef ATOM_PBR_MOTION
    float4 Motion : SV_Target1;
#endif
};

PsOut main(VertexOut pin)
{
    PsOut outVal;
	// Interpolating normal can unnormalize it, so renormalize it.
    pin.NormalW = normalize(pin.NormalW);
	
    // Write normal in view space coordinates
    float3 normalV = mul((float3x3) gView, pin.NormalW);
    outVal.Color = float4(normalV, 1.0f);
#ifdef ATOM_PBR_MOTION
    float3 prevNdc = pin.PrevClip.xyz / max(pin.PrevClip.w, 1e-6f);
    float2 prevUv = float2(prevNdc.x * 0.5f + 0.5f, 0.5f - prevNdc.y * 0.5f);
    bool valid = pin.MotionValid != 0 && pin.PrevClip.w > 1e-6f &&
        prevNdc.z >= 0.0f && prevNdc.z < 1.0f;
    float2 currentUv = pin.PosH.xy * gInvScreenSize;
    // Encode distance from far depth so R16F keeps precision for distant geometry.
    outVal.Motion = float4(prevUv - currentUv, 1.0f - prevNdc.z, valid ? 1.0f : 0.0f);
#endif
    return outVal;
}
