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
    ShpereMaterialHeap,
    ShpereMapHeap = 8,
    ShadowMapHeap = 9,
    SsaoMapHeap = 10,
    NullCubeCbvHeap = 11,
    NullTexSrvHeap1,
    NullTexSrvHeap2, 
    EnvirSrvHeap,
    EnvirUavHeap ,
    PrefilteredEnvirSrvHeap = EnvirUavHeap + 9,
    PrefilteredEnvirUavHeap,    
    IrradianceMapSrvHeap = PrefilteredEnvirUavHeap + 9,
    IrradianceMapUavHeap,
    LUTsrv,
    LUTuav,
    ColorBufferSrv,
    ColorBufferBrightSrv

};

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

    void UpdateSsaoCB(const GameTimer& gt);
    void UpdateUI();

    void InitConstantBuffer();

    void LoadTextures();
    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void DrawSceneToShadowMap();
    void DrawNormalsAndDepth();
    void CreateColorBufferView();
    void CreateCubeMap();
    D3D12_CPU_DESCRIPTOR_HANDLE CreateTextureUav(ID3D12Resource* res, UINT mipSlice);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

private:
    ComPtr<ID3D12RootSignature> m_RootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> m_SrvDescriptorHeap = nullptr;


    std::unordered_map<std::string, ComPtr<ID3DBlob>> m_Shaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_PSOs;
    std::unordered_map<std::string, std::unique_ptr<Texture>> m_Textures;
    
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

    Camera m_Camera;

    std::unique_ptr<ShadowMap> mShadowMap;

    DirectX::BoundingSphere mSceneBounds;


    XMFLOAT4 mLightPosW;
    XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

    float mLightRotationAngle = 0.0f;

    XMFLOAT3 mRotatedLightDirections[3];

    
    ComPtr<ID3D12Resource> m_PrefilteredEnvirMap; // cube map with different roughness mip maps
    ComPtr<ID3D12Resource> m_EnvirMap; // cube map with different mip maps
    ComPtr<ID3D12Resource> m_IrradianceMap;
    ComPtr<ID3D12Resource> m_LUT;

    // color -> color buffer -->(postprocess) ->back buffer 
    // 1 srv with 1 rtv
    ComPtr<ID3D12Resource> m_ColorBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE m_ColorBufferRtvHandle;
    
    ComPtr<ID3D12Resource> m_ColorBufferBright;
    D3D12_CPU_DESCRIPTOR_HANDLE m_ColorBufferBrightRtvHandle;


    BYTE* data = nullptr;


    bool isColorBufferInit = false;
    std::unique_ptr<Ssao> mSsao;

    ShaderParams m_ShaderAttribs;
    EnvMapRenderer::RenderAttribs m_EnvMapAttribs;
    PostProcess::RenderAttribs m_ppAttribs;


    ComPtr<ID3D12Resource> m_ShadowPassGlobalConstantsBuffer;
    ComPtr<ID3D12Resource> m_LightPassGlobalConstantsBuffer;
    std::vector < ComPtr<ID3D12Resource>> m_MaterialConstantsBuffers;
    GlobalConstants m_ShadowPassGlobalConstants = {};
    GlobalConstants m_LightPassGlobalConstants = {};
    std::vector<MeshConstants> m_MeshConstants;
    std::vector<ComPtr<ID3D12Resource>> m_MeshConstantsBuffers;

    std::vector < MaterialConstants> m_MaterialConstants;

    const UINT64 GlobalConstantsBufferSize = sizeof(GlobalConstants);
    const UINT64 MeshConstantsBufferSize = sizeof(MeshConstants);
    const UINT64 MaterialConstantsBufferSize = sizeof(MaterialConstants);


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
