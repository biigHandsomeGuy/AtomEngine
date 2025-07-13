#pragma once

#include "TextureManager.h"

class DescriptorHeap;
class DescriptorHandle;
class RootSignature;
namespace Renderer
{

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

	extern DescriptorHeap s_TextureHeap;
    extern DescriptorHandle m_CommonTextures;
    extern DescriptorHandle g_SSAOSrvHeap;
    extern DescriptorHandle g_SSAOUavHeap;
    extern DescriptorHandle g_PostprocessHeap;
    extern DescriptorHandle g_NullDescriptor;
    extern RootSignature s_RootSig;
    extern std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PSOs;
    extern Microsoft::WRL::ComPtr<ID3D12PipelineState> s_SkyboxPSO;
    void Initialize(void);
    void Shutdown(void);
    void UpdateGlobalDescriptors(void);
}