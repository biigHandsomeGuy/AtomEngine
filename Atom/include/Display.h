#pragma once

#include <cstdint>

namespace Display
{
    void Initialize(void);
    void Shutdown(void);
    void Resize(uint32_t width, uint32_t height);
    void Present(void);
}

namespace Graphics
{
    extern Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_RtvHeap;
    extern Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_DsvHeap;
    extern UINT g_CurrentBuffer;
    extern Microsoft::WRL::ComPtr<ID3D12Resource> g_DisplayPlane[];
    extern Microsoft::WRL::ComPtr<ID3D12Resource> g_DepthStencilBuffer;
    extern uint32_t g_DisplayWidth;
    extern uint32_t g_DisplayHeight;

    extern D3D12_VIEWPORT g_ViewPort;
    extern D3D12_RECT g_Rect;


    extern DXGI_FORMAT SwapChainFormat;
    extern DXGI_FORMAT DepthStencilFormat;

}