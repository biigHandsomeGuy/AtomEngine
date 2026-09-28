#pragma once

#include "Renderer.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "ColorBuffer.h"
#include "Model.h"
#include <filesystem>
#include <unordered_map>

class ModelAssetManager;

namespace Renderer
{
    // Pipeline wrappers are immutable after initialization. Backend PSO/root
    // signature caches retain the device objects for the device lifetime.
    class PbrPipelines
    {
    public:
        void Initialize(PipelineFormats formats);
        bool Matches(PipelineFormats formats) const;
        RootSignature GraphicsRoot;
        RootSignature ComputeRoot;
        std::unordered_map<std::string, GraphicsPSO> Graphics;
        GraphicsPSO Skybox{ L"PBR skybox" };
        ComputePSO Post{ L"PBR temporal post" };
    private:
        PipelineFormats m_Formats;
        bool m_Initialized = false;
    };
}

// Application-owned resources shared by any number of render views on one
// device. Initialize before views; shutdown after all views and GPU work end.
class PbrResources
{
public:
    void Initialize(const std::filesystem::path& projectRoot, ModelAssetManager& assets,
        Renderer::PipelineFormats formats);
    void Shutdown();
    bool Initialized() const { return m_Initialized; }
    uint32_t PrecomputeCount() const { return m_PrecomputeCount; }

    Renderer::PbrPipelines Pipelines;
    Model Skybox;
    ColorBuffer Environment;
    ColorBuffer Radiance;
    ColorBuffer Irradiance;
    ColorBuffer BrdfLut;
    ColorBuffer Emu;
    ColorBuffer Eavg;
    ColorBuffer SssDiffuse;
    ColorBuffer SssSpecular;

private:
    void Precompute(TextureID source);
    bool m_Initialized = false;
    uint32_t m_PrecomputeCount = 0;
};
