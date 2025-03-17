#pragma once


namespace Graphics
{
	// color -> color buffer -->(postprocess) ->back buffer 
    // 1 srv with 1 rtv
    extern Microsoft::WRL::ComPtr<ID3D12Resource> g_SceneColorBuffer;
    extern Microsoft::WRL::ComPtr<ID3D12Resource> g_SceneDepthBuffer;
    extern Microsoft::WRL::ComPtr<ID3D12Resource> g_SceneNormalBuffer;
    extern Microsoft::WRL::ComPtr<ID3D12Resource> g_ShadowBuffer;
    extern Microsoft::WRL::ComPtr<ID3D12Resource> g_SSAOFullScreen;
    extern Microsoft::WRL::ComPtr<ID3D12Resource> g_RandomVectorBuffer;
    
    extern D3D12_CPU_DESCRIPTOR_HANDLE g_SceneColorBufferRtvHandle;
    extern D3D12_CPU_DESCRIPTOR_HANDLE g_SceneNormalBufferRtvHandle;
    extern D3D12_CPU_DESCRIPTOR_HANDLE g_SSAOFullScreenRtvHandle;


    void InitializeRenderingBuffers(uint32_t NativeWidth, uint32_t NativeHeight);
    void DestroyRenderingBuffers();
}