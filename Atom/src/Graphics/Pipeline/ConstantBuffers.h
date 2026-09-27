#pragma once
#include "Math/Matrix3.h"
#include "Math/Matrix4.h"
#include "Math/Vector.h"


__declspec(align(256)) struct MaterialConstants
{
    uint32_t BaseColorIndex;
    uint32_t NormalIndex;
    uint32_t RoughnessMetallicIndex;
    uint32_t EmissiveIndex;
};

__declspec(align(256)) struct GlobalConstants
{
    Math::Matrix4 ViewMatrix{ Math::kIdentity };
    Math::Matrix4 ProjMatrix{ Math::kIdentity };
    Math::Matrix4 ViewProjMatrix{ Math::kIdentity };
    //Math::Matrix4 ViewProjTex{ Math::kIdentity };
    Math::Matrix4 SunShadowMatrix{ Math::kIdentity };
    Math::Vector3 CameraPos = { 0.0f, 0.0f, 0.0f };
    Math::Vector3 SunPos = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT2 Jitter = { 0.0f, 0.0f };
    float Padding0 = 0.0f;
    float Padding1 = 0.0f;
    DirectX::XMFLOAT4 SunDirection{ 0, 0, -1, 0 }; // Direction of emitted rays.
    DirectX::XMFLOAT4 SunRadiance{ 0, 0, 0, 0 }; // Linear RGB color * intensity.
};

__declspec(align(256)) struct SsaoConstants
{
    Math::Matrix4 Proj{ Math::kIdentity };
    Math::Matrix4 InvProj{ Math::kIdentity };
    Math::Matrix4 ProjTex{ Math::kIdentity };
    Math::Vector4   OffsetVectors[14];

    // For SsaoBlur.hlsl
    Math::Vector4 BlurWeights[3];

    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };

    // Coordinates given in view space.
    float OcclusionRadius = 0.5f;
    float OcclusionFadeStart = 0.2f;
    float OcclusionFadeEnd = 2.0f;
    float SurfaceEpsilon = 0.05f;
};



namespace EnvMapRenderer
{
    __declspec(align(256)) struct RenderAttribs
    {
        float EnvMapMipLevel = 0.0f;
    };
}
namespace PostProcess
{
    __declspec(align(256)) struct RenderAttribs
    {
        Math::Matrix4 InvViewProj{ Math::kIdentity };
        Math::Matrix4 PrevViewProj{ Math::kIdentity };
        DirectX::XMFLOAT2 CurrJitter = { 0.0f, 0.0f };
        DirectX::XMFLOAT2 PrevJitter = { 0.0f, 0.0f };
        float exposure = 2.0f;
        bool isRenderingLuminance = false;
        char pad0[3] = { 0,0,0 };
        bool reinhard = false;
        char pad1[3] = { 0,0,0 };
        bool filmic = false;
        char pad2[3] = { 0,0,0 };
        bool aces = true;
        char pad3[3] = { 0,0,0 };
        bool useTAA = true;
        char pad4[3] = { 0,0,0 };
        bool taaHistoryValid = false;
        char pad5[3] = { 0,0,0 };
        float taaFeedback = 0.94f;
        bool useGama = true;
        bool useBloom = false;
        char pad6[3] = { 0,0,0 };
        float bloomThreshold = 1.0f;
        float bloomStrength = 0.15f;
    };
}
