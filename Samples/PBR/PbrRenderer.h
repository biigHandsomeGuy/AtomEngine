#pragma once
#include "CommandContext.h"
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




__declspec(align(256)) struct ShaderParams
{
    bool UseSSAO = false;
    char pad0[3]{ 0,0,0 };
    bool UseShadow = false;
    char pad1[3]{ 0,0,0 };
    bool UseTexture = true;
    char pad2[3]{ 0,0,0 };
    float roughness = 0;
    float albedo[3] = { 0,0,0 };
    float metallic = 0;
    bool UseEmu;
    char pad3[3]{ 0,0,0 };
};

TextureRef g_IBLTexture;

class PbrRenderer : public GameCore::IGameApp
{
public:
    
    PbrRenderer(HINSTANCE hInstance);
    PbrRenderer(const PbrRenderer& rhs) = delete;
    PbrRenderer& operator=(const PbrRenderer& rhs) = delete;
    ~PbrRenderer();

    void OnResize() override;
    void Startup() override;
    void Cleanup()override {};
    // init all resource
    void InitResource();
private:
    virtual void Update(float gt)override;
    virtual void RenderScene()override;

    void UpdateUI();

    void PrecomputeCubemaps(CommandContext& gfxContext);


private:

    std::unordered_map<std::string, ComPtr<ID3DBlob>> m_Shaders;
    std::unordered_map<std::string, std::unique_ptr<Texture>> m_Textures;
    
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

    Camera m_Camera;


    DirectX::BoundingSphere mSceneBounds;


    XMFLOAT4 mLightPosW;
    XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

    float mLightRotationAngle = 0.0f;

    XMFLOAT3 mRotatedLightDirections[3];

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

    std::vector <MaterialConstants> m_MaterialConstants;

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
