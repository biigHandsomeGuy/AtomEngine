#include "../../Core/src/d3dApp.h"
#include "../../Core/src/MathHelper.h"
#include "../../Core/src/UploadBuffer.h" 
#include "../../Core/src/GeometryGenerator.h"
#include "../../Core/src/Camera.h"
#include "ShadowMap.h"
#include "Ssao.h"
#include "../../Core/src/Mesh.h"
#include "../../Core/src/d3dUtil.h"
#include "../../Core/src/Texture.h"
#include "../../Core/src/Mesh.h"
#include <unordered_map>
#include <DirectXCollision.h>
using Microsoft::WRL::ComPtr;
using namespace DirectX;

enum class DescriptorHeapLayout : UINT8
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
    LUTuav

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

    kNumRootBindings      
};


class SsaoApp : public D3DApp
{
public:
    
    SsaoApp(HINSTANCE hInstance);
    SsaoApp(const SsaoApp& rhs) = delete;
    SsaoApp& operator=(const SsaoApp& rhs) = delete;
    ~SsaoApp();

    virtual bool Initialize()override;

private:
    virtual void CreateRtvAndDsvDescriptorHeaps()override;
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);

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
    void CreateCubeMap();
    void CreateIBL();
    D3D12_CPU_DESCRIPTOR_HANDLE CreateTextureUav(ID3D12Resource* res, UINT mipSlice);



    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

private:


    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;


    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

    Camera mCamera;

    std::unique_ptr<ShadowMap> mShadowMap;


    DirectX::BoundingSphere mSceneBounds;


    XMFLOAT4 mLightPosW;
    XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

    float mLightRotationAngle = 0.0f;

    XMFLOAT3 mRotatedLightDirections[3];

    POINT mLastMousePos;
    bool mouseDown = true;
    ComPtr<ID3D12Resource> m_EnvirMap;
    ComPtr<ID3D12Resource> m_EnvirMapUnfiltered;
    ComPtr<ID3D12Resource> m_IrradianceMap;
    ComPtr<ID3D12Resource> m_LUT;

    Model m_PbrModel;
    Model m_SkyBox;
    Model m_Ground;

    BYTE* data = nullptr;

    XMMATRIX m_pbrModelMatrix;
    XMMATRIX m_GroundModelMatrix;

    std::unique_ptr<Ssao> mSsao;

    ShaderParams m_ShaderAttribs;
};

