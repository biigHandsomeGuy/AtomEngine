#pragma once

#include "Atom.h"
#include "MathHelper.h"
#include "GeometryGenerator.h"
#include "Camera.h"
#include "ShadowMap.h"
#include "Ssao.h"
#include "Mesh.h"
#include "d3dUtil.h"
#include "Texture.h"
#include <DirectXCollision.h>


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
    EnvirUnfilterSrvHeap,
    EnvirUnfilterUavHeap ,
    EnvirUnfilterMipMap1,
    EnvirUnfilterMipMap2,
    EnvirUnfilterMipMap3,
    EnvirUnfilterMipMap4,
    EnvirUnfilterMipMap5,
    EnvirSrvHeap,
    EnvirUavHeap,
    IrradianceMapSrvHeap,
    IrradianceMapUavHeap,
    EnvirUavHeapMipMap1,
    EnvirUavHeapMipMap2,
    EnvirUavHeapMipMap3,
    EnvirUavHeapMipMap4,
    EnvirUavHeapMipMap5,
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
    kCommonSRVs,          // cubemap?
    kCommonCBV,           // global cbv
    kShaderParams,
    kCubemapSrv,
    kIrradianceSrv,
    kSpecularSrv,
    kLUT,
    kPostProcess,
    kNumRootBindings      
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


    std::unordered_map<std::string, std::unique_ptr<Texture>> m_Textures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> m_Shaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_PSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

    Camera m_Camera;

    std::unique_ptr<ShadowMap> mShadowMap;

    DirectX::BoundingSphere mSceneBounds;


    XMFLOAT4 mLightPosW;
    XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

    float mLightRotationAngle = 0.0f;

    XMFLOAT3 mRotatedLightDirections[3];

    
    ComPtr<ID3D12Resource> m_EnvirMap; // cube map with different roughness mip maps
    ComPtr<ID3D12Resource> m_EnvirMapUnfiltered; // cube map with different mip maps
    ComPtr<ID3D12Resource> m_IrradianceMap;
    ComPtr<ID3D12Resource> m_LUT;

    // color -> color buffer -->(postprocess) ->back buffer 
    // 1 srv with 1 rtv
    ComPtr<ID3D12Resource> m_ColorBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE m_ColorBufferRtvHandle;
    
    ComPtr<ID3D12Resource> m_ColorBufferBright;
    D3D12_CPU_DESCRIPTOR_HANDLE m_ColorBufferBrightRtvHandle;



    Model m_PbrModel;
    Model m_SkyBox;
    Model m_Ground;

    BYTE* data = nullptr;

    XMMATRIX m_pbrModelMatrix;
    XMMATRIX m_GroundModelMatrix;
    bool isColorBufferInit = false;
    std::unique_ptr<Ssao> mSsao;

    ShaderParams m_ShaderAttribs;
    EnvMapRenderer::RenderAttribs m_EnvMapAttribs;
    PostProcess::RenderAttribs m_ppAttribs;


    ComPtr<ID3D12Resource> m_GlobalConstantsBuffer;
    GlobalConstants m_GlobalConstants = {};
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
