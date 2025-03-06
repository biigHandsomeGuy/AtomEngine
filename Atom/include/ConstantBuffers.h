#pragma once


__declspec(align(256)) struct MeshConstants
{
    DirectX::XMFLOAT4X4 ModelMatrix;
    DirectX::XMFLOAT4X4 NormalMatrix;
    DirectX::XMFLOAT4X4 ViewProjTex;
};
__declspec(align(256)) struct MaterialConstants
{
    UINT gMatIndex;
};

__declspec(align(256)) struct GlobalConstants
{
    DirectX::XMFLOAT4X4 ViewMatrix;
    DirectX::XMFLOAT4X4 ProjMatrix;
    DirectX::XMFLOAT4X4 ViewProjMatrix;
    DirectX::XMFLOAT4X4 SunShadowMatrix;
    DirectX::XMFLOAT3 CameraPos = { 0.0f, 0.0f, 0.0f };
    float pad0 = 0;
    DirectX::XMFLOAT3 SunPos = { 0.0f, 0.0f, 0.0f };
};

__declspec(align(256)) struct SsaoConstants
{
    DirectX::XMFLOAT4X4 Proj;
    DirectX::XMFLOAT4X4 InvProj;
    DirectX::XMFLOAT4X4 ProjTex;
    DirectX::XMFLOAT4   OffsetVectors[14];

    // For SsaoBlur.hlsl
    DirectX::XMFLOAT4 BlurWeights[3];

    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };

    // Coordinates given in view space.
    float OcclusionRadius = 0.5f;
    float OcclusionFadeStart = 0.2f;
    float OcclusionFadeEnd = 2.0f;
    float SurfaceEpsilon = 0.05f;
};

__declspec(align(256)) struct ShaderParams
{
    bool UseSSAO = false; 
    char pad0[3]{0,0,0};
    bool UseShadow = false;
    char pad1[3]{0,0,0};
    bool UseTexture = false;
    char pad2[3]{ 0,0,0 };
    float roughness = 0;
    float albedo[3] = { 0,0,0 };
    float metallic = 0;
    
    
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
    };
}