#pragma once

#include "TextureManager.h"

class DescriptorHeap;
class DescriptorHandle;
class RootSignature;
class GraphicsPSO;
class ComputePSO;
namespace Renderer
{
    struct PipelineFormats
    {
        DXGI_FORMAT Color = DXGI_FORMAT_R16G16B16A16_FLOAT;
        DXGI_FORMAT Depth = DXGI_FORMAT_D24_UNORM_S8_UINT;
        DXGI_FORMAT Normal = DXGI_FORMAT_R16G16B16A16_FLOAT;
        DXGI_FORMAT Shadow = DXGI_FORMAT_D16_UNORM;
        DXGI_FORMAT Motion = DXGI_FORMAT_R16G16B16A16_FLOAT;
    };
    extern DescriptorHeap s_TextureHeap;
    extern DescriptorHandle m_CommonTextures;
    extern DescriptorHandle g_PostProcessTexture;
    extern DescriptorHandle g_SSAOSrvHeap;
    extern DescriptorHandle g_SSAOUavHeap;
    extern DescriptorHandle g_NullDescriptor;
    extern RootSignature s_RootSig;
    extern RootSignature s_ComputeRootSig;
    extern RootSignature g_BindlessRootSig;

    extern std::unordered_map<std::string, GraphicsPSO> s_PSOs;
    extern GraphicsPSO s_SkyboxPSO;
    extern ComputePSO s_PostProcessPSO;



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

	

    void Initialize(bool useLegacyDescriptors = true, PipelineFormats formats = {});
    void Shutdown(void);
    void UpdateGlobalDescriptors(void);
}
