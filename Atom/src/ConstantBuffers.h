#pragma once
#include "Math/Matrix3.h"
#include "Math/Matrix4.h"
#include "Math/Vector.h"

__declspec(align(256)) struct MeshConstants
{
    Math::Matrix4 ModelMatrix{ Math::kIdentity };
    Math::Matrix4 NormalMatrix{ Math::kIdentity };
    Math::Matrix4 ViewProjTex{ Math::kIdentity };
};
__declspec(align(256)) struct MaterialConstants
{
    uint32_t gMatIndex;
};

__declspec(align(256)) struct GlobalConstants
{
    Math::Matrix4 ViewMatrix{ Math::kIdentity };
    Math::Matrix4 ProjMatrix{ Math::kIdentity };
    Math::Matrix4 ViewProjMatrix{ Math::kIdentity };
    Math::Matrix4 SunShadowMatrix{ Math::kIdentity };
    Math::Vector3 CameraPos = { 0.0f, 0.0f, 0.0f };
    Math::Vector3 SunPos = { 0.0f, 0.0f, 0.0f };
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
        float exposure = 2.0f;
        bool isRenderingLuminance = false;
        char pad0[3] = { 0,0,0 };
        bool reinhard = false;
        char pad1[3] = { 0,0,0 };
        bool filmic = false;
        char pad2[3] = { 0,0,0 };
        bool aces = true;
        char pad3[3] = { 0,0,0 };
    };
}