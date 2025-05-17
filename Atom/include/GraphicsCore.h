#pragma once

#include "DescriptorHeap.h"

class CommandListManager;
class ContextManager;
namespace Graphics
{

    void Initialize(bool RequireDXRSupport = false);
    void Shutdown(void);
    

    extern unsigned int RtvDescriptorSize;
    extern unsigned int DsvDescriptorSize;
    extern unsigned int CbvSrvUavDescriptorSize;

    extern Microsoft::WRL::ComPtr<ID3D12Device> g_Device;
    // extern ID3D12GraphicsCommandList* g_CommandList;
    // extern ID3D12CommandAllocator* g_CommandAllocator;
    extern CommandListManager g_CommandManager;
    extern ContextManager g_ContextManager;

    extern DescriptorAllocator g_DescriptorAllocator[];
    inline D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE Type, UINT Count = 1)
    {
        return g_DescriptorAllocator[Type].Allocate(Count);
    }
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
    PrefilteredEnvirSrvHeap = EnvirUavHeap + 10,
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
    SsaoUav,
    EmuSrv,
    EmuUav,
    EavgSrv,
    EavgUav
};