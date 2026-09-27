#pragma once
#include "CommandContext.h"
#include "MathHelper.h"
#include "Camera.h"
#include "RenderView.h"
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
    PbrRenderer(Scene& scene, RenderView& view);
    PbrRenderer(const PbrRenderer& rhs) = delete;
    PbrRenderer& operator=(const PbrRenderer& rhs) = delete;
    void Initialize();
    void Shutdown();
    bool ResizeView(uint32_t width, uint32_t height);
    void Render(GraphicsContext& graphics);

    const Math::Camera& Camera() const { return m_View.Camera; }
    float& CameraNearClip() { return m_View.NearClip; }
    float& CameraFarClip() { return m_View.FarClip; }
    ShaderParams& ShadingSettings() { return m_ShaderAttribs; }
    EnvMapRenderer::RenderAttribs& EnvironmentSettings() { return m_EnvMapAttribs; }
    PostProcess::RenderAttribs& PostSettings() { return m_ppAttribs; }

private:
	void SetupGeometryGraph();
	void SetupLightingGraph();
	void SetupPostProcessGraphs();

    void PrecomputeCubemaps();
    void UpdateViewDescriptors();


private:
    ShadowCamera m_SunShadowCamera;
    float mLightRotationAngle = 0.0f;

    Vector3 mRotatedLightDirections{ kIdentity };

    ShaderParams m_ShaderAttribs;
    EnvMapRenderer::RenderAttribs m_EnvMapAttribs;
    PostProcess::RenderAttribs m_ppAttribs;

    GlobalConstants m_ShadowPassGlobalConstants = {};
    GlobalConstants m_LightPassGlobalConstants = {};

    Scene& m_Scene;
    RenderView& m_View;
    std::unique_ptr<SSAO::ViewPasses> m_SsaoPasses;
    DescriptorHandle m_CommonTextures;
    DescriptorHandle m_PostProcessTextures;
    RenderGraph m_GeometryGraph;
    RenderGraph m_LightingGraph;
    RenderGraph m_PostProcessGraphs[2];
    SkyBox m_SkyBox;
};
