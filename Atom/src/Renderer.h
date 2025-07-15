#pragma once

#include "TextureManager.h"

class DescriptorHeap;
class DescriptorHandle;
class RootSignature;
class GraphicsPSO;
namespace Renderer
{
    extern DescriptorHeap s_TextureHeap;
    extern DescriptorHandle m_CommonTextures;
    extern DescriptorHandle g_SSAOSrvHeap;
    extern DescriptorHandle g_SSAOUavHeap;
    extern DescriptorHandle g_PostprocessHeap;
    extern DescriptorHandle g_NullDescriptor;
    extern RootSignature s_RootSig;

    extern std::unordered_map<std::string, GraphicsPSO> s_PSOs;
    extern GraphicsPSO s_SkyboxPSO;



    enum RootBindings
    {
        kMeshConstants,       // for VS
        kMaterialConstants,   // for PS
        kMaterialSRVs,        // material texture 
        kCommonSRVs,          //
        kCommonCBV,           // global cbv
        kPostprocessSRVs,
        kShaderParams,
        kNumRootBindings
    };

	

    void Initialize(void);
    void Shutdown(void);
    void UpdateGlobalDescriptors(void);
}