#include "pch.h"
#include "RenderView.h"

#include <algorithm>

bool RenderView::Resize(uint32_t width, uint32_t height)
{
    // Zero represents a collapsed/hidden panel. Preserve its previous view.
    if (width == 0 || height == 0)
        return false;
    width = std::clamp(width, 8u, 16384u);
    height = std::clamp(height, 8u, 16384u);
    if (Width() == width && Height() == height)
        return false;

    const uint32_t halfWidth = (width + 1) / 2;
    const uint32_t halfHeight = (height + 1) / 2;
    SceneColor.Create(L"View / HDR color", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
    Output.Create(L"View / Output", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
    History[0].Create(L"View / Temporal history 0", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
    History[1].Create(L"View / Temporal history 1", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
    SceneDepth.Create(L"View / Depth", width, height, DXGI_FORMAT_D24_UNORM_S8_UINT);
    SceneNormal.Create(L"View / Normals", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
    if (!Shadow.GetResource())
        Shadow.Create(L"View / Shadow", 2048, 2048, DXGI_FORMAT_D16_UNORM);
    LinearDepthFull.Create(L"View / Linear depth", width, height, 1, DXGI_FORMAT_R32_FLOAT);
    LinearDepthHalf.Create(L"View / Half linear depth", halfWidth, halfHeight, 1, DXGI_FORMAT_R32_FLOAT);
    SsaoFull.Create(L"View / Occlusion", width, height, 1, DXGI_FORMAT_R8_UNORM);
    SsaoHalf.Create(L"View / Half occlusion", halfWidth, halfHeight, 1, DXGI_FORMAT_R8_UNORM);
    SsaoBlur.Create(L"View / Occlusion blur", halfWidth, halfHeight, 1, DXGI_FORMAT_R8_UNORM);

    Viewport = { 0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f };
    Scissor = { 0, 0, LONG(width), LONG(height) };
    Camera.SetAspectRatio(float(height) / float(width));
    EditorCamera.SetAspectRatio(float(height) / float(width));
    EditorCamera.Update();
    Camera.Update();
    InvalidateHistory();
    return true;
}

void RenderView::InvalidateHistory()
{
    HistoryValid = false;
    HistoryReadIndex = 0;
    FrameIndex = 0;
    PreviousViewProjection = Camera.GetViewProjMatrix();
    PreviousJitter = {};
    Jitter = {};
}

void RenderView::Destroy()
{
    SceneColor.Destroy();
    Output.Destroy();
    for (auto& history : History)
        history.Destroy();
    SceneDepth.Destroy();
    SceneNormal.Destroy();
    Shadow.Destroy();
    LinearDepthFull.Destroy();
    LinearDepthHalf.Destroy();
    SsaoFull.Destroy();
    SsaoHalf.Destroy();
    SsaoBlur.Destroy();
    InvalidateHistory();
}
