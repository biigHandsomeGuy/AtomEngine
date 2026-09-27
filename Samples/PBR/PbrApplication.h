#pragma once

#include <cstdint>
#include <memory>

#include "GameCore.h"
#include "PbrRenderer.h"
#include "CameraController.h"
#include "ModelAssetManager.h"
#include "FrameScheduler.h"

struct ViewportImage
{
    uint64_t TextureId = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
};

struct ViewportRequest
{
    uint32_t Width = 0;
    uint32_t Height = 0;
};

struct ApplicationValidation
{
    uint32_t Errors = 0;
    uint32_t Completed = 0;
    uint32_t Expected = 0;
};

// Applications supply UI without making the renderer depend on editor code.
class PbrApplicationUi
{
public:
    virtual ~PbrApplicationUi() = default;
    virtual void Initialize(Scene& scene, ModelAssetManager& assets) {}
    virtual void StartValidation(const Scene& scene, ModelAssetManager& assets) {}
    virtual ApplicationValidation Validation() const { return {}; }
    virtual ViewportRequest Draw(Scene& scene, PbrRenderer& renderer, const ViewportImage& image) = 0;
    virtual void Shutdown() {}
};

class PbrApplication final : public GameCore::IGameApp
{
public:
    explicit PbrApplication(std::unique_ptr<PbrApplicationUi> ui, uint32_t smokeFrameLimit = 0);

    void Startup() override;
    void Cleanup() override;
    void Update(float deltaTime) override;
    void OnResize() override;
    void RenderScene() override;
    void OnFramePresented() override;
    bool IsDone() override;
    int ExitCode() const override { return m_ExitCode; }
    bool UsesLegacySceneBuffers() const override { return false; }

    static uint32_t ParseSmokeFrameLimit(const wchar_t* commandLine);

private:
    void InitializeUi();
    void CheckViewIsolation();
    void StartAssetChecks();
    void PollAssetChecks();
    void CheckFrameScheduler();
    void CheckAssetCache();
    void CheckSceneEntities();
    void PrepareSceneViewChecks();
    void PollSceneViewChecks(uint64_t secondViewFrameBefore);
    void CaptureLightProbe(GraphicsContext& graphics);
    void FinishLightProbe(uint64_t fence);
    void SceneCheck(bool passed, const char* label);

    Scene m_Scene;
    ModelAssetManager m_Assets;
    FrameScheduler m_Frames;
    RenderView m_View;
    PbrResources m_RenderResources;
    PbrRenderer m_Renderer;
    RenderView m_SecondView;
    std::unique_ptr<PbrRenderer> m_SecondRenderer;
    std::unique_ptr<CameraController> m_CameraController;
    std::unique_ptr<PbrApplicationUi> m_Ui;
    DescriptorHandle m_ViewportDescriptors[FrameScheduler::FramesInFlight];
    std::vector<DescriptorHandle> m_FreeUiDescriptors;
    bool m_UiInitialized = false;
    uint32_t m_SmokeFrameLimit = 0;
    uint64_t m_CompletedFrames = 0;
    int m_ExitCode = 0;
    uint32_t m_ViewErrors = 0;
    uint32_t m_ResizeChecks = 0;
    uint32_t m_WindowResizes = 0;
    AssetRequest m_AssetRequests[6]{};
    uint32_t m_AssetErrors = 0;
    uint32_t m_AssetChecks = 0;
    bool m_UploadCancelRequested = false;
    uint32_t m_FrameErrors = 0;
    uint32_t m_FrameChecks = 0;
    uint32_t m_PresentationChecks = 0;
    uint32_t m_MultiViewChecks = 0;
    uint64_t m_SecondViewFrames = 0;
    uint32_t m_CacheErrors = 0;
    uint32_t m_CacheChecks = 0;
    uint32_t m_SceneErrors = 0;
    uint32_t m_SceneChecks = 0;
    std::optional<CameraEntity> m_CheckCamera;
    std::optional<DirectionalLightEntity> m_CheckLight;
    EntityId m_SecondSceneCamera = InvalidEntityId;
    std::shared_ptr<ReadbackBuffer> m_LightProbe;
    uint64_t m_LightProbeFrame = 0;
    uint32_t m_LightProbePitch = 0;
    bool m_LightPixelsValid = false;
    std::vector<float> m_LightPixels;
};
