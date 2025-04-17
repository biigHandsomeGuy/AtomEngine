#pragma once

#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Camera.h"
#include "Ssao.h"

#include "d3dUtil.h"
#include "Scene.h"
#include "SkyBox.h"
#include <DirectXCollision.h>
#include "GameCore.h"


using Microsoft::WRL::ComPtr;
using namespace DirectX;



enum RootBindings
{
    kMeshConstants,       // for VS
    kMaterialConstants,   // for PS
    kMaterialSRVs,        // material texture 
    kCommonSRVs,          // sphere map / shadow map / ssao map 
    kCommonCBV,           // global cbv
    kShaderParams,
    kCubemapSrv,
    kIrradianceSrv,
    kSpecularSrv,
    kLUT,
    kPostProcess,
    kNumRootBindings      
};
__declspec(align(256)) struct ShaderParams
{
    bool UseSSAO = false;
    char pad0[3]{ 0,0,0 };
    bool UseShadow = false;
    char pad1[3]{ 0,0,0 };
    bool UseTexture = false;
    char pad2[3]{ 0,0,0 };
    float roughness = 0;
    float albedo[3] = { 0,0,0 };
    float metallic = 0;
};

class Renderer : public GameCore::IGameApp
{
public:
    
    Renderer(HINSTANCE hInstance);
    Renderer(const Renderer& rhs) = delete;
    Renderer& operator=(const Renderer& rhs) = delete;
    ~Renderer();

    void OnResize() override;
    void Startup() override;
    void Cleanup()override {};
    // init all resource
    void InitResource();
private:
    virtual void Update(float gt)override;
    virtual void RenderScene()override;

    void UpdateUI();

  
    void LoadTextures(ID3D12CommandList* CmdList);
    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void CreateCubeMap(ID3D12GraphicsCommandList* CmdList);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

private:
    ComPtr<ID3D12RootSignature> m_RootSignature = nullptr;


    std::unordered_map<std::string, ComPtr<ID3DBlob>> m_Shaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_PSOs;
    std::unordered_map<std::string, std::unique_ptr<Texture>> m_Textures;
    
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

    Camera m_Camera;


    DirectX::BoundingSphere mSceneBounds;


    XMFLOAT4 mLightPosW;
    XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

    float mLightRotationAngle = 0.0f;

    XMFLOAT3 mRotatedLightDirections[3];

    
    ComPtr<ID3D12Resource> m_PrefilteredEnvirMap; // cube map with different roughness mip maps
    ComPtr<ID3D12Resource> m_EnvirMap; // cube map with different mip maps
    ComPtr<ID3D12Resource> m_IrradianceMap;
    ComPtr<ID3D12Resource> m_LUT;

    BYTE* data = nullptr;

    ShaderParams m_ShaderAttribs;
    EnvMapRenderer::RenderAttribs m_EnvMapAttribs;
    PostProcess::RenderAttribs m_ppAttribs;
    
    // ComPtr<ID3D12Resource> m_
    ComPtr<ID3D12Resource> m_ShadowPassGlobalConstantsBuffer;
    ComPtr<ID3D12Resource> m_LightPassGlobalConstantsBuffer;
    std::vector<ComPtr<ID3D12Resource>> m_MaterialConstantsBuffers;
    GlobalConstants m_ShadowPassGlobalConstants = {};
    GlobalConstants m_LightPassGlobalConstants = {};
    std::vector<MeshConstants> m_MeshConstants;
    std::vector<ComPtr<ID3D12Resource>> m_MeshConstantsBuffers;

    std::vector < MaterialConstants> m_MaterialConstants;

    const UINT64 GlobalConstantsBufferSize = sizeof(GlobalConstants);
    const UINT64 MeshConstantsBufferSize = sizeof(MeshConstants);
    const UINT64 MaterialConstantsBufferSize = sizeof(MaterialConstants);

    // shaderParameter cbuffer
    ComPtr<ID3D12Resource> shaderParamsCbuffer;
    // Post Process
    ComPtr<ID3D12Resource> ppBuffer;
    // material cbuffer
    ComPtr<ID3D12Resource> envMapBuffer;

    UINT64 shaderParamBufferSize = 0;

    UINT64 PostProcessBufferSize = 0;
    UINT64 EnvMapAttribsBufferSize = 0;


    Scene m_Scene;
    SkyBox m_SkyBox;
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
