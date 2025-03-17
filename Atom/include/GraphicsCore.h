#pragma once

class CommandListManager;
namespace Graphics
{

    void Initialize(bool RequireDXRSupport = false);
    void Shutdown(void);
    void FlushCommandQueue();

    extern unsigned int RtvDescriptorSize;
    extern unsigned int DsvDescriptorSize;
    extern unsigned int CbvSrvUavDescriptorSize;

    extern Microsoft::WRL::ComPtr<ID3D12Device> g_Device;
    // extern CommandListManager g_CommandManager;
    extern Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> g_CommandList;
    extern Microsoft::WRL::ComPtr<ID3D12CommandAllocator> g_CommandAllocator;
    extern Microsoft::WRL::ComPtr<ID3D12CommandQueue> g_CommandQueue;
    extern Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_SrvHeap;
    extern Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_RtvHeap;
    extern Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_DsvHeap;
    extern Microsoft::WRL::ComPtr<ID3D12Fence> g_Fence;
}

enum class DescriptorHeapLayout : int
{
    ShpereMaterialHeap,
    ShpereMapHeap = 8,
    ShadowBufferSrv = 9,
    SsaoMapHeap = 10,
    NullCubeCbvHeap = 11,
    NullTexSrvHeap1,
    NullTexSrvHeap2,
    EnvirSrvHeap,
    EnvirUavHeap,
    PrefilteredEnvirSrvHeap = EnvirUavHeap + 9,
    PrefilteredEnvirUavHeap,
    IrradianceMapSrvHeap = PrefilteredEnvirUavHeap + 9,
    IrradianceMapUavHeap,
    LUTsrv,
    LUTuav,
    SceneColorBufferSrv,
    SceneNormalBufferSrv,
    SceneDepthBufferSrv,   
    RandomVectorMapSrv
};