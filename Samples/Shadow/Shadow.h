#pragma once

#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Camera.h"
#include "ShadowMap.h"
#include "Ssao.h"

#include "d3dUtil.h"
#include "Scene.h"
#include "SkyBox.h"
#include <DirectXCollision.h>
#include "Application.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

enum class DescriptorHeapLayout : int
{
    ShadowMapHeap
};

enum RootBindings
{
    kMeshConstants,       // for VS
    kMaterialConstants,   // for PS
    kCommonSRVs,          // shadow map
    kCommonCBV,           // global cbv
    kShaderParams,
    kNumRootBindings
};
__declspec(align(256)) struct ShaderParams
{
    // float4
    float albedo[3] = { 0,0,0 };
    bool UseBasicShadow = 0;
    char pad0[3] = { 0,0,0 };

    // float2
    bool UsePCFShadow = 0;
    char pad1[3] = { 0,0,0 };
    bool UsePCSSShadow = 0;
    char pad2[3] = { 0,0,0 };

    // float
    float LightWidth = 10;
};

class Renderer : public Application
{
public:

    Renderer(HINSTANCE hInstance);
    Renderer(const Renderer& rhs) = delete;
    Renderer& operator=(const Renderer& rhs) = delete;
    ~Renderer();

    virtual bool Initialize()override;

private:
    virtual void CreateRtvAndDsvDescriptorHeaps()override;
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    void UpdateUI();

    void InitConstantBuffer();

    void LoadTextures();
    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void DrawSceneToShadowMap();

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

private:
    ComPtr<ID3D12RootSignature> m_RootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> m_SrvDescriptorHeap = nullptr;


    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_PSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    Camera m_Camera;

    std::unique_ptr<ShadowMap> mShadowMap;

    DirectX::BoundingSphere mSceneBounds;


    XMFLOAT4 mLightPosW;
    XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

    float mLightRotationAngle = 0.0f;

    XMFLOAT3 mRotatedLightDirections[3];

    BYTE* data = nullptr;

    ShaderParams m_ShaderAttribs;

    ComPtr<ID3D12Resource> m_ShadowPassGlobalConstantsBuffer;
    ComPtr<ID3D12Resource> m_LightPassGlobalConstantsBuffer;

    GlobalConstants m_ShadowPassGlobalConstants = {};
    GlobalConstants m_LightPassGlobalConstants = {};
    std::vector<MeshConstants> m_MeshConstants;
    std::vector<ComPtr<ID3D12Resource>> m_MeshConstantsBuffers;


    const UINT64 GlobalConstantsBufferSize = sizeof(GlobalConstants);
    const UINT64 MeshConstantsBufferSize = sizeof(MeshConstants);

    Scene m_Scene;
};

namespace
{

    inline const char* c_str(const std::string& str)
    {
        return str.c_str();
    }

    inline const char* c_str(const char* str)
    {
        return str;
    }

} // namespace
