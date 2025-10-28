#include "pch.h"
#include "Display.h"

#include "GraphicsCore.h"
#include "CommandListManager.h"
#include "BufferManager.h"
#include "ColorBuffer.h"
#include "SystemTime.h"
using namespace Graphics;
using namespace Microsoft::WRL;

#define SWAP_CHAIN_BUFFER_COUNT 3

namespace GameCore
{
    extern HWND g_hWnd;
}

namespace Graphics
{
    float s_FrameTime = 0.0f;
    uint64_t s_FrameIndex = 0;
    int64_t s_FrameStartTick = 0;


    DXGI_FORMAT SwapChainFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;


	IDXGISwapChain1* s_SwapChain1 = nullptr;
    UINT g_CurrentBuffer = 0;

	uint32_t g_DisplayWidth = 1080;
	uint32_t g_DisplayHeight = 720;

    XMFLOAT2 g_RendererSize = { (float)g_DisplayWidth , (float)g_DisplayHeight };
    D3D12_VIEWPORT g_ViewPort;
    D3D12_RECT g_Rect;

    ColorBuffer g_DisplayPlane[SWAP_CHAIN_BUFFER_COUNT];   
}

void Display::Initialize(void)
{
	assert(s_SwapChain1 == nullptr);
    // Update the viewport transform to cover the client area.
    g_ViewPort.TopLeftX = 0;
    g_ViewPort.TopLeftY = 0;
    g_ViewPort.Width = static_cast<float>(g_DisplayWidth);
    g_ViewPort.Height = static_cast<float>(g_DisplayHeight);
    g_ViewPort.MinDepth = 0.0f;
    g_ViewPort.MaxDepth = 1.0f;

    g_Rect = { 0, 0, (long)g_DisplayWidth, (long)g_DisplayHeight };

	Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;
	ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = g_DisplayWidth;
    swapChainDesc.Height = g_DisplayHeight;
    swapChainDesc.Format = SwapChainFormat;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Scaling = DXGI_SCALING_NONE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
    fsSwapChainDesc.Windowed = TRUE;

    ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
        g_CommandManager.GetCommandQueue(),
        GameCore::g_hWnd,
        &swapChainDesc,
        &fsSwapChainDesc,
        nullptr,
        &s_SwapChain1));

    
    for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        ComPtr<ID3D12Resource> displayPlane;
        ThrowIfFailed(s_SwapChain1->GetBuffer(i, IID_PPV_ARGS(&displayPlane)));
        g_DisplayPlane[i].CreateFromSwapChain(L"Primary SwapChain Buffer", displayPlane.Detach());
    }

    InitializeRenderingBuffers(g_DisplayWidth, g_DisplayHeight);
}

void Display::Shutdown(void)
{
    s_SwapChain1->SetFullscreenState(FALSE, nullptr);
    s_SwapChain1->Release();

    for (uint32_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
    {
        g_DisplayPlane[i].Destroy();
    }
}


void Display::Resize(uint32_t width, uint32_t height)
{
    width = std::max(width, 8u);
    height = std::max(height, 8u);


        g_CommandManager.IdleGPU();
        g_DisplayWidth = width;
        g_DisplayHeight = height;

        g_ViewPort.TopLeftX = 0;
        g_ViewPort.TopLeftY = 0;
        g_ViewPort.Width = static_cast<float>(g_DisplayWidth);
        g_ViewPort.Height = static_cast<float>(g_DisplayHeight);
        g_ViewPort.MinDepth = 0.0f;
        g_ViewPort.MaxDepth = 1.0f;

        g_Rect = { 0, 0, (long)g_DisplayWidth, (long)g_DisplayHeight };


        for (uint32_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
        {
            g_DisplayPlane[i].Destroy();
        }
        // Resize the swap chain.
        ThrowIfFailed(s_SwapChain1->ResizeBuffers(
            SWAP_CHAIN_BUFFER_COUNT,
            g_DisplayWidth, g_DisplayHeight,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

        g_CurrentBuffer = 0;

        for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
        {
            ComPtr<ID3D12Resource> displayPlane;
            ThrowIfFailed(s_SwapChain1->GetBuffer(i, IID_PPV_ARGS(&displayPlane)));
            g_DisplayPlane[i].CreateFromSwapChain(L"Primary SwapChain Buffer", displayPlane.Detach());

        }

        ResizeDisplayDependentBuffers(g_DisplayWidth, g_DisplayHeight);
        g_CommandManager.IdleGPU();
    
    
}


void Display::Present(void)
{
    s_SwapChain1->Present(0, 0);
    g_CurrentBuffer = (g_CurrentBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;
    int64_t CurrentTick = SystemTime::GetCurrentTick();
    s_FrameTime = (float)SystemTime::TimeBetweenTicks(s_FrameStartTick, CurrentTick);


    s_FrameStartTick = CurrentTick;

    ++s_FrameIndex;


}
uint64_t Graphics::GetFrameCount(void)
{
    return s_FrameIndex;
}

float Graphics::GetFrameTime(void)
{
    return s_FrameTime;
}

float Graphics::GetFrameRate(void)
{
    return s_FrameTime == 0.0f ? 0.0f : 1.0f / s_FrameTime;
}