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
#include "PbrResources.h"
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

struct ScreenSpaceGiSettings
{
    bool Enabled = false;
    float Radius = 4.0f;
    float Thickness = 0.35f;
    float Intensity = 1.0f;
    uint32_t Rays = 6;
    uint32_t Steps = 16;
};

class PbrRenderer
{
public:
    // Shared resources outlive every view. Finish GPU use before Shutdown;
    // ResizeView waits for GPU completion before rebuilding its attachments.
    PbrRenderer(Scene& scene, RenderView& view, PbrResources& resources);
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
    ScreenSpaceGiSettings& GiSettings() { return m_GiSettings; }
    EnvMapRenderer::RenderAttribs& EnvironmentSettings() { return m_EnvMapAttribs; }
    PostProcess::RenderAttribs& PostSettings() { return m_ppAttribs; }
    const PbrResources& SharedResources() const { return m_Resources; }
    const Math::Camera& EditorCamera() const { return m_View.EditorCamera; }
    Math::Camera& NavigationCamera() { return m_View.EditorCamera; }
    bool UsesSceneCamera() const { return m_View.UseSceneCamera; }
    EntityId SceneCameraSource() const { return m_View.SceneCameraId; }
    void SetSceneCameraSource(EntityId id)
    {
        if (m_View.SceneCameraId != id) { m_View.SceneCameraId = id; m_View.InvalidateHistory(); }
    }
    void UseSceneCamera(bool enabled)
    {
        if (m_View.UseSceneCamera != enabled) { m_View.UseSceneCamera = enabled; m_View.InvalidateHistory(); }
    }
    EntityId ResolvedCameraId() const { return m_ResolvedCamera; }
    EntityId ResolvedLightId() const { return m_ResolvedLight; }
    bool UsesSceneShadows() const { return m_ShaderAttribs.UseShadow && m_LightCastsShadows; }
    const GlobalConstants& SceneConstants() const { return m_LightPassGlobalConstants; }

private:
	void SetupGeometryGraph();
	void SetupLightingGraph();
    void SetupGiGraph();
	void SetupPostProcessGraphs();

    void UpdateViewDescriptors();
    void UpdateGiPostDescriptors();
    void ExtractSceneView();


private:
    ShadowCamera m_SunShadowCamera;

    ShaderParams m_ShaderAttribs;
    ScreenSpaceGiSettings m_GiSettings;
    ScreenSpaceGiSettings m_LastGiSettings;
    EnvMapRenderer::RenderAttribs m_EnvMapAttribs;
    PostProcess::RenderAttribs m_ppAttribs;

    GlobalConstants m_ShadowPassGlobalConstants = {};
    GlobalConstants m_LightPassGlobalConstants = {};

    Scene& m_Scene;
    RenderView& m_View;
    PbrResources& m_Resources;
    bool m_Initialized = false;
    uint64_t m_LastSceneRevision = UINT64_MAX;
    EntityId m_ResolvedCamera = InvalidEntityId;
    EntityId m_ResolvedLight = InvalidEntityId;
    bool m_LightCastsShadows = false;
    Math::Vector3 m_ShadowCenter{ 0, 0, 0 };
    Math::Vector3 m_ShadowBounds{ 1, 1, 1 };
    std::unique_ptr<SSAO::ViewPasses> m_SsaoPasses;
    DescriptorHandle m_CommonTextures;
    DescriptorHandle m_PostProcessTextures[2][2]; // history index, GI disabled/enabled
    RenderGraph m_GeometryGraph;
    RenderGraph m_LightingGraphs[2];
    RenderGraph m_GiGraph;
    RenderGraph m_PostProcessGraphs[2][2];
};
