#include "pch.h"
#include "Display.h"

#include "GraphicsCore.h"
#include "CommandListManager.h"
using namespace Graphics;

#define SWAP_CHAIN_BUFFER_COUNT 3


namespace GameCore
{
    extern HWND g_hWnd;
}

namespace Graphics
{
    DXGI_FORMAT SwapChainFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;


	IDXGISwapChain1* s_SwapChain1 = nullptr;
    UINT g_CurrentBuffer = 0;

	uint32_t g_DisplayWidth = 1080;
	uint32_t g_DisplayHeight = 720;

    D3D12_VIEWPORT g_ViewPort;
    D3D12_RECT g_Rect;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_RtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_DsvHeap;

    Microsoft::WRL::ComPtr<ID3D12Resource> g_DisplayPlane[SWAP_CHAIN_BUFFER_COUNT];
    Microsoft::WRL::ComPtr<ID3D12Resource> g_DepthStencilBuffer;
    
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

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SWAP_CHAIN_BUFFER_COUNT + 2;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(g_Device->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(&g_RtvHeap)));


    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 1 + 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    ThrowIfFailed(g_Device->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(&g_DsvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(g_RtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        ThrowIfFailed(s_SwapChain1->GetBuffer(i, IID_PPV_ARGS(&g_DisplayPlane[i])));
        g_Device->CreateRenderTargetView(g_DisplayPlane[i].Get(), nullptr, rtvHeapHandle);
        rtvHeapHandle.Offset(1, Graphics::RtvDescriptorSize);
    }

    // Create the depth/stencil buffer and view.
    D3D12_RESOURCE_DESC depthStencilDesc;
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Alignment = 0;
    depthStencilDesc.Width = g_DisplayWidth;
    depthStencilDesc.Height = g_DisplayHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = DepthStencilFormat;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = DepthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;
    ThrowIfFailed(g_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(&g_DepthStencilBuffer)));
    g_DepthStencilBuffer->SetName(L"g_DepthStencilBuffer");
    // Create descriptor to mip level 0 of entire resource using the format of the resource.
    g_Device->CreateDepthStencilView(g_DepthStencilBuffer.Get(), nullptr, g_DsvHeap->GetCPUDescriptorHandleForHeapStart());
    // Transition the resource from its initial state to be used as a depth buffer.
    g_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_DepthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));
    g_CommandManager.IdleGPU();
}

void Display::Shutdown(void)
{
}

void Display::Resize(uint32_t width, uint32_t height)
{
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
        g_DisplayPlane[i].Reset();
    }
    // Resize the swap chain.
    ThrowIfFailed(s_SwapChain1->ResizeBuffers(
        SWAP_CHAIN_BUFFER_COUNT,
        g_DisplayWidth, g_DisplayHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

    g_CurrentBuffer = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(g_RtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        ThrowIfFailed(s_SwapChain1->GetBuffer(i, IID_PPV_ARGS(&g_DisplayPlane[i])));
        g_Device->CreateRenderTargetView(g_DisplayPlane[i].Get(), nullptr, rtvHeapHandle);
        rtvHeapHandle.Offset(1, Graphics::RtvDescriptorSize);
    }

    g_CommandList->RSSetViewports(1, &g_ViewPort);

    g_CommandList->RSSetScissorRects(1, &g_Rect);


    // g_CommandManager.GetGraphicsQueue().ExecuteCommandList(g_CommandList);
    g_CommandManager.IdleGPU();
}

void Display::Present(void)
{
   
    g_CommandList->RSSetViewports(1, &g_ViewPort);
   
    g_CommandList->RSSetScissorRects(1, &g_Rect);


    // Indicate a state transition on the resource usage.
    g_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_DisplayPlane[g_CurrentBuffer].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    g_CommandManager.GetQueue().ExecuteCommandList(g_CommandList);

    s_SwapChain1->Present(1, 0);
    g_CurrentBuffer = (g_CurrentBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;

    g_CommandManager.IdleGPU();
}
