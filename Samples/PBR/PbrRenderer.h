#pragma once
#include "CommandContext.h"
#include "MathHelper.h"
#include "Camera.h"
#include "CameraController.h"
#include "ShadowCamera.h"
#include "Ssao.h"

#include "Scene.h"
#include "SkyBox.h"
#include <DirectXCollision.h>
#include "GameCore.h"
#include <iostream>


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
    float roughness = 0.5;
    float albedo[3] = { 0.5,0.3,0.1 };
    float metallic = 0.5;
    bool UseEmu;
    char pad3[3]{ 0,0,0 };
    bool UseSSS;
    char pad4[3]{ 0,0,0 };
    float CurveFactor = {};
    float SpecularFactor = {};
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
    Math::Camera m_Camera;
    std::unique_ptr<CameraController> m_CameraController;
    ShadowCamera m_SunShadowCamera;
    float mLightRotationAngle = 0.0f;

    Vector3 mRotatedLightDirections{ kIdentity };

    ShaderParams m_ShaderAttribs;
    EnvMapRenderer::RenderAttribs m_EnvMapAttribs;
    PostProcess::RenderAttribs m_ppAttribs;

    GlobalConstants m_ShadowPassGlobalConstants = {};
    GlobalConstants m_LightPassGlobalConstants = {};

    std::vector<MaterialConstants> m_MaterialConstants;

    Scene m_Scene;
    SkyBox m_SkyBox;
    DescriptorHandle m_BackBufferHandle[3];
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
