#include "../../Core/src/d3dApp.h"
#include "../../Core/src/MathHelper.h"
#include "../../Core/src/UploadBuffer.h" 
#include "../../Core/src/GeometryGenerator.h"
#include "../../Core/src/Camera.h"
#include "FrameResource.h"
#include "ShadowMap.h"
#include "Ssao.h"
#include "../../Core/src/Mesh.h"
#include "../../Core/src/d3dUtil.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

enum class DescriptorHeapLayout : UINT8
{
    ShpereMaterialHeap,
    ShpereMapHeap = 4,
    ShadowMapHeap = 5,
    SsaoMapHeap = 6,
    NullCubeCbvHeap = 11,
    NullTexSrvHeap1 = 12,
    NullTexSrvHeap2 = 13, 
    EnvirUnfilterSrvHeap = 14,
    EnvirUnfilterUavHeap = 15,
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


struct MeshBuffer
{
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    D3D12_INDEX_BUFFER_VIEW ibv;
    UINT numElements;
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
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialBuffer(const GameTimer& gt);
    void UpdateShadowTransform(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateShadowPassCB(const GameTimer& gt);
    void UpdateSsaoCB(const GameTimer& gt);

    void LoadTextures();
    void BuildRootSignature();
    void BuildSsaoRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildSkullGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawSceneToShadowMap();
    void DrawNormalsAndDepth();
    void CreateCubeMap();
    void CreateIBL();
    D3D12_CPU_DESCRIPTOR_HANDLE CreateTextureUav(ID3D12Resource* res, UINT mipSlice);
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv(int index)const;
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrv(int index)const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsv(int index)const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtv(int index)const;
    MeshBuffer CreateMeshBuffer(const std::unique_ptr<class Mesh>& mesh);


    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12RootSignature> mSsaoRootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;


    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    //std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout_Pos_UV;


    CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

    PassConstants mMainPassCB;  // index 0 of pass cbuffer.
    PassConstants mShadowPassCB;// index 1 of pass cbuffer.

    Camera mCamera;

    std::unique_ptr<ShadowMap> mShadowMap;

    std::unique_ptr<Ssao> mSsao;

    DirectX::BoundingSphere mSceneBounds;

    float mLightNearZ = 0.0f;
    float mLightFarZ = 0.0f;
    XMFLOAT3 mLightPosW;
    XMFLOAT4X4 mLightView = MathHelper::Identity4x4();
    XMFLOAT4X4 mLightProj = MathHelper::Identity4x4();
    XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

    float mLightRotationAngle = 0.0f;
    XMFLOAT3 mBaseLightDirections[3] = {
        XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
        XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
        XMFLOAT3(0.0f, -0.707f, -0.707f)
    };
    XMFLOAT3 mRotatedLightDirections[3];

    POINT mLastMousePos;
    bool mouseDown = true;
    ComPtr<ID3D12Resource> m_EnvirMap;
    ComPtr<ID3D12Resource> m_EnvirMapUnfiltered;
    ComPtr<ID3D12Resource> m_IrradianceMap;
    ComPtr<ID3D12Resource> m_LUT;

    MeshBuffer m_PbrModel;
    MeshBuffer m_SkyBox;

    BYTE* data = nullptr;
};

