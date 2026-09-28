#pragma once

#include "Camera.h"
#include "ColorBuffer.h"
#include "DepthBuffer.h"

// Resources and camera history belong to a view, independently of presentation.
// Resize/Destroy require the caller to finish GPU work using this view first.
class RenderView
{
public:
    RenderView() = default;
    RenderView(const RenderView&) = delete;
    RenderView& operator=(const RenderView&) = delete;

    bool Resize(uint32_t width, uint32_t height);
    void Destroy();
    void InvalidateHistory();
    uint32_t Width() const { return SceneColor.GetResource() ? SceneColor.GetWidth() : 0; }
    uint32_t Height() const { return SceneColor.GetResource() ? SceneColor.GetHeight() : 0; }

    Math::Camera Camera;
    Math::Camera EditorCamera; // Navigation survives switching to a scene camera.
    bool UseSceneCamera = false;
    uint64_t SceneCameraId = 0; // Zero follows the scene's active camera.
    float NearClip = 0.1f;
    float FarClip = 10000.0f;
    D3D12_VIEWPORT Viewport{};
    D3D12_RECT Scissor{};

    ColorBuffer SceneColor;
    ColorBuffer Output;
    ColorBuffer History[2];
    ColorBuffer HistoryDepth[2];
    ColorBuffer Motion;
    DepthBuffer SceneDepth;
    ColorBuffer SceneNormal;
    DepthBuffer Shadow;
    ColorBuffer LinearDepthFull;
    ColorBuffer LinearDepthHalf;
    ColorBuffer SsaoFull;
    ColorBuffer SsaoHalf;
    ColorBuffer SsaoBlur;

    uint32_t HistoryReadIndex = 0;
    uint64_t FrameIndex = 0;
    bool HistoryValid = false;
    bool LastUseTAA = false;
    Math::Matrix4 PreviousViewProjection{ Math::kIdentity };
    DirectX::XMFLOAT2 PreviousJitter{};
    DirectX::XMFLOAT2 Jitter{};
};
