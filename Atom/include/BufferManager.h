#pragma once

struct ColorBuffer
{
    Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
    D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE SrvHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE UavHandle[12] = { 0 };
};

struct DepthBuffer
{
    Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
    D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE SrvHandle;
};


namespace Graphics
{
    extern ColorBuffer g_SceneColorBuffer;
    extern DepthBuffer g_SceneDepthBuffer;
    extern ColorBuffer g_SceneNormalBuffer;
    extern DepthBuffer g_ShadowBuffer;
    extern ColorBuffer g_SSAOFullScreen;
    extern ColorBuffer g_SSAOUnBlur;
    extern ColorBuffer g_RandomVectorBuffer;

    extern ColorBuffer g_RadianceMap; // cube map with different roughness mip maps
    extern ColorBuffer g_EnvirMap; // cube map with different mip maps
    extern ColorBuffer g_IrradianceMap;
    extern ColorBuffer g_LUT;
    extern ColorBuffer g_Emu;
    extern ColorBuffer g_Eavg;

    void InitializeRenderingBuffers(uint32_t NativeWidth, uint32_t NativeHeight);
    void ResizeDisplayDependentBuffers(uint32_t NativeWidth, uint32_t NativeHeight);
    void DestroyRenderingBuffers();
}