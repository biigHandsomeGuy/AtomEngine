#pragma once
#include "CommandContext.h"
#include "MathHelper.h"
#include "Camera.h"
#include "CameraController.h"
#include "ShadowCamera.h"
#include "Ssao.h"

#include "SceneGraph.h"
#include "SkyBox.h"
#include "RenderGraph.h"
#include "ConstantBuffers.h"
#include <DirectXCollision.h>


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
    float roughness = 0.5f;
    float albedo[3] = { 0.5f, 0.3f, 0.1f };
    float metallic = 0.5f;
    bool UseEmu = false;
    char pad3[3]{ 0,0,0 };
    bool UseSSS = false;
    char pad4[3]{ 0,0,0 };
    float CurveFactor = {};
    float SpecularFactor = {};
};

inline TextureID g_IBLTexture = INVALID_TEXTURE;

class PbrRenderer
{
public:
    explicit PbrRenderer(Scene& scene);
    PbrRenderer(const PbrRenderer& rhs) = delete;
    PbrRenderer& operator=(const PbrRenderer& rhs) = delete;
    void Initialize();
    void Shutdown();
    void OnResize();
    void Update(float deltaTime);
    void Render(GraphicsContext& graphics);

    const Math::Camera& Camera() const { return m_Camera; }
    float& CameraNearClip() { return m_CameraNearClip; }
    float& CameraFarClip() { return m_CameraFarClip; }
    ShaderParams& ShadingSettings() { return m_ShaderAttribs; }
    EnvMapRenderer::RenderAttribs& EnvironmentSettings() { return m_EnvMapAttribs; }
    PostProcess::RenderAttribs& PostSettings() { return m_ppAttribs; }

private:
	void SetupGeometryGraph();
	void SetupLightingGraph();
	void SetupPostProcessGraphs();

    void PrecomputeCubemaps();


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

    Scene& m_Scene;
    RenderGraph m_GeometryGraph;
    RenderGraph m_LightingGraph;
    RenderGraph m_PostProcessGraphs[2];
    DirectX::XMFLOAT2 m_FrameJitter = { 0.0f, 0.0f };
    SkyBox m_SkyBox;
};
