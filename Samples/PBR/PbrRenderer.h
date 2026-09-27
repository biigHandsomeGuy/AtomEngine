#pragma once
#include "CommandContext.h"
#include "MathHelper.h"
#include "Camera.h"
#include "CameraController.h"
#include "ShadowCamera.h"
#include "Ssao.h"

#include "Scene.h"
#include "SkyBox.h"
#include "..\Editor\EditorPanels.h"
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

inline TextureID g_IBLTexture = INVALID_TEXTURE;

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
    float m_CameraNearClip = 0.1f;
    float m_CameraFarClip = 10000.0f;
    std::unique_ptr<CameraController> m_CameraController;
    ShadowCamera m_SunShadowCamera;
    float mLightRotationAngle = 0.0f;
    uint32_t m_TaaHistoryReadIndex = 0;
    uint64_t m_FrameIndex = 0;
    bool m_TaaHistoryValid = false;
    bool m_LastUseTAA = false;
    Math::Matrix4 m_PrevViewProj{ Math::kIdentity };
    DirectX::XMFLOAT2 m_PrevJitter = { 0.0f, 0.0f };

    Vector3 mRotatedLightDirections{ kIdentity };

    ShaderParams m_ShaderAttribs;
    EnvMapRenderer::RenderAttribs m_EnvMapAttribs;
    PostProcess::RenderAttribs m_ppAttribs;

    GlobalConstants m_ShadowPassGlobalConstants = {};
    GlobalConstants m_LightPassGlobalConstants = {};

    Scene m_Scene;
    AtomEditor::UiState m_EditorUi;
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
