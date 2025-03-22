#pragma once

class CommandListManager;
namespace Graphics
{

    void Initialize(bool RequireDXRSupport = false);
    void Shutdown(void);
    

    extern unsigned int RtvDescriptorSize;
    extern unsigned int DsvDescriptorSize;
    extern unsigned int CbvSrvUavDescriptorSize;

    extern Microsoft::WRL::ComPtr<ID3D12Device> g_Device;
    extern CommandListManager g_CommandManager;
    extern ID3D12GraphicsCommandList* g_CommandList;
    extern ID3D12CommandAllocator* g_CommandAllocator;
    extern Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_SrvHeap;
    extern Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_RtvHeap;
    extern Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_DsvHeap;
}

enum class DescriptorHeapLayout : int
{
    MaterialSrv,
    ShpereMapHeap = 4,
    ShadowBufferSrv = 5,
    SsaoMapHeap = 6,
    NullCubeCbvHeap = 7,
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
    RandomVectorMapSrv,
    SsaoTempSrv,
    SsaoUav
};