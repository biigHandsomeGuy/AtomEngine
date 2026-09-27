#pragma once

#include <cstdint>
#include <memory>

#include "GameCore.h"
#include "PbrRenderer.h"
#include "CameraController.h"
#include "ModelAssetManager.h"

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

// Applications supply UI without making the renderer depend on editor code.
class PbrApplicationUi
{
public:
    virtual ~PbrApplicationUi() = default;
    virtual void Initialize(Scene& scene, ModelAssetManager& assets) {}
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
    bool IsDone() override;
    int ExitCode() const override { return m_ExitCode; }
    bool UsesLegacySceneBuffers() const override { return false; }

    static uint32_t ParseSmokeFrameLimit(const wchar_t* commandLine);

private:
    void InitializeUi();
    void CheckViewIsolation();
    void StartAssetChecks();
    void PollAssetChecks();

    Scene m_Scene;
    ModelAssetManager m_Assets;
    RenderView m_View;
    PbrRenderer m_Renderer;
    std::unique_ptr<CameraController> m_CameraController;
    std::unique_ptr<PbrApplicationUi> m_Ui;
    DescriptorHandle m_ViewportDescriptors[3];
    bool m_UiInitialized = false;
    uint32_t m_SmokeFrameLimit = 0;
    uint64_t m_CompletedFrames = 0;
    int m_ExitCode = 0;
    uint32_t m_ViewErrors = 0;
    uint32_t m_ResizeChecks = 0;
    uint32_t m_WindowResizes = 0;
    AssetRequestId m_AssetRequests[6]{};
    uint32_t m_AssetErrors = 0;
    uint32_t m_AssetChecks = 0;
    bool m_UploadCancelRequested = false;
};
