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
    bool UseSSS;
    char pad4[3]{ 0,0,0 };
    float Intensity = {};
    float Thickness = {};
    float S = {};
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

private:
    virtual void Update(float gt)override;
    virtual void RenderScene()override;

    void UpdateUI();

    void PrecomputeCubemaps(CommandContext& gfxContext);


private:
    Camera m_Camera;

    DirectX::BoundingSphere mSceneBounds;

    XMFLOAT4 mLightPosW;
    XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

    float mLightRotationAngle = 0.0f;

    XMFLOAT3 mRotatedLightDirections[3];


    ShaderParams m_ShaderAttribs;
    EnvMapRenderer::RenderAttribs m_EnvMapAttribs;
    PostProcess::RenderAttribs m_ppAttribs;

    GlobalConstants m_ShadowPassGlobalConstants = {};
    GlobalConstants m_LightPassGlobalConstants = {};
    std::vector<MeshConstants> m_MeshConstants;

    std::vector<MaterialConstants> m_MaterialConstants;

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
