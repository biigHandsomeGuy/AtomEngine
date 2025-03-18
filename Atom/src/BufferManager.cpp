#include "pch.h"
#include "BufferManager.h"
#include "GraphicsCore.h"
#include "Display.h"
#include "CommandListManager.h"
DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
DXGI_FORMAT DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
namespace Graphics
{
    
    Microsoft::WRL::ComPtr<ID3D12Resource> g_SceneColorBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> g_SceneDepthBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> g_SceneNormalBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> g_ShadowBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> g_SSAOFullScreen;
    Microsoft::WRL::ComPtr<ID3D12Resource> g_RandomVectorBuffer;

    D3D12_CPU_DESCRIPTOR_HANDLE g_SceneColorBufferRtvHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE g_SceneNormalBufferRtvHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE g_SSAOFullScreenRtvHandle;

    void InitializeRenderingBuffers(uint32_t NativeWidth, uint32_t NativeHeight)
    {
        // g_CommandManager.CreateNewCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, &g_CommandList, &g_CommandAllocator);


        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D12_RESOURCE_DESC desc = {};

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = BackBufferFormat; // 你的RTV格式
        clearValue.Color[0] = 0.0f;  // R
        clearValue.Color[1] = 0.0f;  // G
        clearValue.Color[2] = 0.0f;  // B
        clearValue.Color[3] = 1.0f;  // A

        desc.Width = NativeWidth;
        desc.Height = NativeHeight;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Format = BackBufferFormat;
        desc.MipLevels = 1;
        desc.DepthOrArraySize = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;

        ThrowIfFailed(g_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            &clearValue,
            IID_PPV_ARGS(&g_SceneColorBuffer)));
        g_SceneColorBuffer->SetName(L"g_SceneColorBuffer");

        // Create rtv
        
        rtvDesc.Format = BackBufferFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.Texture2D.PlaneSlice = 0;

        g_SceneColorBufferRtvHandle = GetCpuHandle(g_RtvHeap.Get(), 3);

        g_Device->CreateRenderTargetView(
            g_SceneColorBuffer.Get(),
            &rtvDesc,
            g_SceneColorBufferRtvHandle
        );


        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Format = BackBufferFormat;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = -1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0;

        auto srvHandle = GetCpuHandle(g_SrvHeap.Get(), (int)DescriptorHeapLayout::SceneColorBufferSrv);

        g_Device->CreateShaderResourceView(
            g_SceneColorBuffer.Get(),
            &srvDesc,
            srvHandle);

        // g_SceneNormalBuffer srv + rtv
        desc.Width = NativeWidth;
        desc.Height = NativeHeight;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.MipLevels = 1;
        desc.DepthOrArraySize = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        
        ThrowIfFailed(g_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            &clearValue,
            IID_PPV_ARGS(&g_SceneNormalBuffer)));
        g_SceneNormalBuffer->SetName(L"g_SceneNormalBuffer");

        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvHandle = GetCpuHandle(g_SrvHeap.Get(), (int)DescriptorHeapLayout::SceneNormalBufferSrv);
        g_Device->CreateShaderResourceView(g_SceneNormalBuffer.Get(), &srvDesc, srvHandle);

        g_SceneNormalBufferRtvHandle = GetCpuHandle(g_RtvHeap.Get(), 4);
        rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        g_Device->CreateRenderTargetView(g_SceneNormalBuffer.Get(), &rtvDesc, g_SceneNormalBufferRtvHandle);

        D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
        depthOptimizedClearValue.Format = DepthStencilFormat;
        depthOptimizedClearValue.DepthStencil.Depth = 1.0f;  // 清除深度 = 1.0（默认）
        depthOptimizedClearValue.DepthStencil.Stencil = 0;   // 清除模板 = 0

        // g_DepthStencilBuffer srv + dsv
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

        ThrowIfFailed(g_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &depthStencilDesc,
            D3D12_RESOURCE_STATE_COMMON,
            &depthOptimizedClearValue,
            IID_PPV_ARGS(&g_SceneDepthBuffer)));
        g_SceneDepthBuffer->SetName(L"g_SceneDepthBuffer");
        // Create descriptor to mip level 0 of entire resource using the format of the resource.
        g_Device->CreateDepthStencilView(g_SceneDepthBuffer.Get(), nullptr, g_DsvHeap->GetCPUDescriptorHandleForHeapStart());
        srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srvHandle = GetCpuHandle(g_SrvHeap.Get(), (int)DescriptorHeapLayout::SceneDepthBufferSrv);
        g_Device->CreateShaderResourceView(g_SceneDepthBuffer.Get(), &srvDesc, srvHandle);
        //g_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SceneDepthBuffer.Get(),
        //    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

        // g_ShadowBuffer
        ThrowIfFailed(g_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &depthStencilDesc,
            D3D12_RESOURCE_STATE_COMMON,
            &depthOptimizedClearValue,
            IID_PPV_ARGS(&g_ShadowBuffer)));
        g_ShadowBuffer->SetName(L"g_ShadowBuffer");
        //g_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_ShadowBuffer.Get(),
        //    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));
        srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srvHandle = GetCpuHandle(g_SrvHeap.Get(), (int)DescriptorHeapLayout::ShadowBufferSrv);
        g_Device->CreateShaderResourceView(g_ShadowBuffer.Get(), &srvDesc, srvHandle);

        g_Device->CreateDepthStencilView(g_ShadowBuffer.Get(), nullptr, CD3DX12_CPU_DESCRIPTOR_HANDLE(g_DsvHeap->GetCPUDescriptorHandleForHeapStart(), 1, DsvDescriptorSize));
        clearValue.Format = DXGI_FORMAT_R8_UNORM;
        // g_SSAOFullScreen
        desc.Width = NativeWidth;
        desc.Height = NativeHeight;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.MipLevels = 1;
        desc.DepthOrArraySize = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        ThrowIfFailed(g_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            &clearValue,
            IID_PPV_ARGS(&g_SSAOFullScreen)));
        g_SSAOFullScreen->SetName(L"g_SSAOFullScreen");

        srvDesc.Format = DXGI_FORMAT_R8_UNORM;
        srvHandle = GetCpuHandle(g_SrvHeap.Get(), (int)DescriptorHeapLayout::SsaoMapHeap);
        g_Device->CreateShaderResourceView(g_SSAOFullScreen.Get(), &srvDesc, srvHandle);

        g_SSAOFullScreenRtvHandle = GetCpuHandle(g_RtvHeap.Get(), 5);
        rtvDesc.Format = DXGI_FORMAT_R8_UNORM;
        g_Device->CreateRenderTargetView(g_SSAOFullScreen.Get(), &rtvDesc, g_SSAOFullScreenRtvHandle);


    }

    void DestroyRenderingBuffers()
    {


    }

}