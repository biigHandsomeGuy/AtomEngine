#pragma once

#include "TextureManager.h"

class DescriptorHeap;
class DescriptorHandle;
namespace Renderer
{

    enum RootBindings
    {
        kMeshConstants,       // for VS
        kMaterialConstants,   // for PS
        kMaterialSRVs,        // material texture 
        kCommonSRVs,          //
        kCommonCBV,           // global cbv
        kShaderParams,
        kNumRootBindings
    };

	extern DescriptorHeap s_TextureHeap;
    extern DescriptorHandle m_CommonTextures;
    extern DescriptorHandle g_SSAOSrvHeap;
    extern DescriptorHandle g_SSAOUavHeap;
    void Initialize(void);
    void Shutdown(void);
    void UpdateGlobalDescriptors(void);
}