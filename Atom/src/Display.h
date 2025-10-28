#pragma once

#include <cstdint>

namespace Display
{
    void Initialize(void);
    void Shutdown(void);
    void Resize(uint32_t width, uint32_t height);
    void Present(void);
}
class ColorBuffer;
namespace Graphics
{
    
    extern UINT g_CurrentBuffer;
    extern ColorBuffer g_DisplayPlane[];
    extern uint32_t g_DisplayWidth;
    extern uint32_t g_DisplayHeight;

    extern DirectX::XMFLOAT2 g_RendererSize;
    extern D3D12_VIEWPORT g_ViewPort;
    extern D3D12_RECT g_Rect;

    extern DXGI_FORMAT SwapChainFormat;
    extern DXGI_FORMAT DepthStencilFormat;

    // Returns the number of elapsed frames since application start
    uint64_t GetFrameCount(void);

    // The amount of time elapsed during the last completed frame.  The CPU and/or
    // GPU may be idle during parts of the frame.  The frame time measures the time
    // between calls to present each frame.
    float GetFrameTime(void);

    // The total number of frames per second
    float GetFrameRate(void);


}