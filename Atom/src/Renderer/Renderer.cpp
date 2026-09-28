#include "pch.h"
#include "Renderer.h"
#include "PbrResources.h"
#include <stdexcept>
#include "TextureManager.h"
#include "GraphicsCore.h"
#include "DescriptorHeap.h"
#include "BufferManager.h"
#include "RootSignature.h"
#include "GraphicsCommon.h"
#include "Display.h"
#include "PipelineState.h"
#include "Model.h"

#include "../CompiledShaders/PBRShadingVS.h"
#include "../CompiledShaders/SkyBoxVS.h"
#include "../CompiledShaders/ShadowVS.h"
#include "../CompiledShaders/DrawNormalsVS.h"
#include "../CompiledShaders/PbrMotionVS.h"
#include "../CompiledShaders/PostProcessCS.h"
#include "../CompiledShaders/PbrTemporalCS.h"
#include "../CompiledShaders/PBRShadingPS.h"
#include "../CompiledShaders/SkyBoxPS.h"
#include "../CompiledShaders/ShadowPS.h"
#include "../CompiledShaders/DrawNormalsPS.h"
#include "../CompiledShaders/PbrMotionPS.h"

using namespace Graphics;

namespace Renderer
{
    DescriptorHeap s_TextureHeap;
    DescriptorHandle m_CommonTextures;
    DescriptorHandle g_PostProcessTexture;
    DescriptorHandle g_SSAOSrvHeap;
    DescriptorHandle g_SSAOUavHeap;
    DescriptorHandle g_NullDescriptor;

    RootSignature s_RootSig;
    RootSignature s_ComputeRootSig;
    RootSignature g_BindlessRootSig;

    std::unordered_map<std::string, GraphicsPSO> s_PSOs;
    GraphicsPSO s_SkyboxPSO(L"Skybox PSO");
    ComputePSO s_PostProcessPSO(L"Post Process PSO");

    static void CreateGraphicsRootSignature(RootSignature& s_RootSig)
    {
        s_RootSig.Reset(kNumRootBindings, 5);
        s_RootSig[kMeshConstants].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
        s_RootSig[kMaterialConstants].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
        s_RootSig[kMaterialSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 4, D3D12_SHADER_VISIBILITY_PIXEL);
        s_RootSig[kCommonSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 10, D3D12_SHADER_VISIBILITY_PIXEL);
        s_RootSig[kCommonCBV].InitAsConstantBuffer(1);
        s_RootSig[kPostprocessSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 20, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        s_RootSig[kShaderParams].InitAsConstantBuffer(2, D3D12_SHADER_VISIBILITY_PIXEL);

        s_RootSig.InitStaticSampler(0, SamplerLinearWrapDesc);
        s_RootSig.InitStaticSampler(1, SamplerLinearClampDesc);
        s_RootSig.InitStaticSampler(2, SamplerAnisotropicWrapDesc);
        s_RootSig.InitStaticSampler(3, SamplerAnisotropicClampDesc);
        s_RootSig.InitStaticSampler(4, SamplerShadowDesc);
        s_RootSig.Finalize(
            L"Renderer Graphics RootSig",
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
    }

    static void CreateComputeRootSignature(RootSignature& s_ComputeRootSig, bool pbrTemporal = false)
    {
        s_ComputeRootSig.Reset(3, 4);
        s_ComputeRootSig[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, pbrTemporal ? 5 : 3);
        s_ComputeRootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, pbrTemporal ? 3 : 1);
        s_ComputeRootSig[2].InitAsConstantBuffer(0);

        s_ComputeRootSig.InitStaticSampler(0, SamplerLinearWrapDesc);
        s_ComputeRootSig.InitStaticSampler(1, SamplerLinearClampDesc);
        s_ComputeRootSig.InitStaticSampler(2, SamplerAnisotropicWrapDesc);
        s_ComputeRootSig.InitStaticSampler(3, SamplerAnisotropicClampDesc);
        s_ComputeRootSig.Finalize(pbrTemporal ? L"PBR Temporal RootSig" : L"Renderer Compute RootSig");
    }

    static void CreateBindlessRootSignature()
    {
        g_BindlessRootSig.Reset(1, 5);
        g_BindlessRootSig[0].InitAsConstants(0, 64);
        g_BindlessRootSig.InitStaticSampler(0, SamplerLinearWrapDesc);
        g_BindlessRootSig.InitStaticSampler(1, SamplerLinearClampDesc);
        g_BindlessRootSig.InitStaticSampler(2, SamplerAnisotropicWrapDesc);
        g_BindlessRootSig.InitStaticSampler(3, SamplerAnisotropicClampDesc);
        g_BindlessRootSig.InitStaticSampler(4, SamplerShadowDesc);
        g_BindlessRootSig.Finalize(
            L"BindlessRootSig",
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
            D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);
    }

    static void CreatePSOs(const PipelineFormats& formats, RootSignature& s_RootSig, RootSignature& s_ComputeRootSig,
        std::unordered_map<std::string, GraphicsPSO>& s_PSOs, GraphicsPSO& s_SkyboxPSO, ComputePSO& s_PostProcessPSO,
        bool pbrTemporal = false)
    {
        static const D3D12_INPUT_ELEMENT_DESC kInputLayout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, Position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, Normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, UV)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, Tangent)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, Bitangent)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "CLUSTERID", 0, DXGI_FORMAT_R32_UINT, 0, static_cast<UINT>(offsetof(Vertex, ClusterID)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        GraphicsPSO opaque(L"Opaque PSO");
        opaque.SetRootSignature(s_RootSig);
        opaque.SetRasterizerState(RasterizerDefault);
        opaque.SetBlendState(CD3DX12_BLEND_DESC(D3D12_DEFAULT));

        opaque.SetDepthStencilState(DepthStateReadWrite);
        opaque.SetInputLayout(_countof(kInputLayout), kInputLayout);
        opaque.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        opaque.SetRenderTargetFormat(formats.Color, formats.Depth);
        opaque.SetVertexShader(g_pPBRShadingVS, sizeof(g_pPBRShadingVS));
        opaque.SetPixelShader(g_pPBRShadingPS, sizeof(g_pPBRShadingPS));
        opaque.Finalize();
        s_PSOs["opaque"] = std::move(opaque);

        GraphicsPSO drawNormals(L"DrawNormals PSO");
        drawNormals.SetRootSignature(s_RootSig);
        drawNormals.SetRasterizerState(RasterizerDefault);
        drawNormals.SetBlendState(CD3DX12_BLEND_DESC(D3D12_DEFAULT));
        drawNormals.SetDepthStencilState(DepthStateReadWrite);
        drawNormals.SetInputLayout(_countof(kInputLayout), kInputLayout);
        drawNormals.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        if (pbrTemporal)
        {
            const DXGI_FORMAT targets[] = { formats.Normal, formats.Motion };
            drawNormals.SetRenderTargetFormats(2, targets, formats.Depth);
            drawNormals.SetVertexShader(g_pPbrMotionVS, sizeof(g_pPbrMotionVS));
            drawNormals.SetPixelShader(g_pPbrMotionPS, sizeof(g_pPbrMotionPS));
        }
        else
        {
            drawNormals.SetRenderTargetFormat(formats.Normal, formats.Depth);
            drawNormals.SetVertexShader(g_pDrawNormalsVS, sizeof(g_pDrawNormalsVS));
            drawNormals.SetPixelShader(g_pDrawNormalsPS, sizeof(g_pDrawNormalsPS));
        }
        drawNormals.Finalize();
        s_PSOs["drawNormals"] = std::move(drawNormals);

        GraphicsPSO shadow(L"Shadow PSO");
        shadow.SetRootSignature(s_RootSig);
        shadow.SetRasterizerState(RasterizerShadow);
        shadow.SetBlendState(CD3DX12_BLEND_DESC(D3D12_DEFAULT));
        D3D12_DEPTH_STENCIL_DESC shadowDepthState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        shadowDepthState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        shadowDepthState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        shadow.SetDepthStencilState(shadowDepthState);
        shadow.SetInputLayout(_countof(kInputLayout), kInputLayout);
        shadow.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        shadow.SetRenderTargetFormats(0, nullptr, formats.Shadow);
        shadow.SetVertexShader(g_pShadowVS, sizeof(g_pShadowVS));
        shadow.SetPixelShader(g_pShadowPS, sizeof(g_pShadowPS));
        shadow.Finalize();
        s_PSOs["shadow"] = std::move(shadow);

        s_SkyboxPSO.SetRootSignature(s_RootSig);
        s_SkyboxPSO.SetRasterizerState(RasterizerTwoSided);
        s_SkyboxPSO.SetBlendState(CD3DX12_BLEND_DESC(D3D12_DEFAULT));
        s_SkyboxPSO.SetDepthStencilState(DepthStateReadOnly);
        s_SkyboxPSO.SetInputLayout(_countof(kInputLayout), kInputLayout);
        s_SkyboxPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        s_SkyboxPSO.SetRenderTargetFormat(formats.Color, formats.Depth);
        s_SkyboxPSO.SetVertexShader(g_pSkyBoxVS, sizeof(g_pSkyBoxVS));
        s_SkyboxPSO.SetPixelShader(g_pSkyBoxPS, sizeof(g_pSkyBoxPS));
        s_SkyboxPSO.Finalize();

        s_PostProcessPSO.SetRootSignature(s_ComputeRootSig);
        if (pbrTemporal)
            s_PostProcessPSO.SetComputeShader(g_pPbrTemporalCS, sizeof(g_pPbrTemporalCS));
        else
            s_PostProcessPSO.SetComputeShader(g_pPostProcessCS, sizeof(g_pPostProcessCS));
        s_PostProcessPSO.Finalize();
    }

    bool PbrPipelines::Matches(PipelineFormats formats) const
    {
        return m_Initialized && formats.Color == m_Formats.Color && formats.Depth == m_Formats.Depth &&
            formats.Normal == m_Formats.Normal && formats.Motion == m_Formats.Motion &&
            formats.Shadow == m_Formats.Shadow;
    }

    void PbrPipelines::Initialize(PipelineFormats formats)
    {
        if (m_Initialized)
        {
            if (!Matches(formats)) throw std::logic_error("PBR pipelines require matching attachment formats.");
            return;
        }
        CreateGraphicsRootSignature(GraphicsRoot);
        CreateComputeRootSignature(ComputeRoot, true);
        CreatePSOs(formats, GraphicsRoot, ComputeRoot, Graphics, Skybox, Post, true);
        m_Formats = formats;
        m_Initialized = true;
    }
    void Initialize(bool useLegacyDescriptors, PipelineFormats formats)
    {
        m_CommonTextures = Graphics::g_BindlessHeap.Alloc(10);
        g_PostProcessTexture = Graphics::g_BindlessHeap.Alloc(4);
        g_SSAOSrvHeap = Graphics::g_BindlessHeap.Alloc(4);
        g_SSAOUavHeap = Graphics::g_BindlessHeap.Alloc(1);
        g_NullDescriptor = Graphics::g_BindlessHeap.Alloc(1);

        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
        nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullSrv.Texture2D.MipLevels = 1;
        g_Device->CreateShaderResourceView(nullptr, &nullSrv, g_NullDescriptor);

        CreateGraphicsRootSignature(s_RootSig);
        CreateComputeRootSignature(s_ComputeRootSig);
        CreateBindlessRootSignature();
        CreatePSOs(formats, s_RootSig, s_ComputeRootSig, s_PSOs, s_SkyboxPSO, s_PostProcessPSO);
        if (useLegacyDescriptors)
            UpdateGlobalDescriptors();
    }

    void Shutdown(void)
    {
        s_PSOs.clear();
    }

    void UpdateGlobalDescriptors(void)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE src[10] =
        {
            g_EnvirMap.GetSRV(),
            g_RadianceMap.GetSRV(),
            g_IrradianceMap.GetSRV(),
            g_SSAOFullScreen.GetSRV(),
            g_ShadowBuffer.GetDepthSRV(),
            g_LUT.GetSRV(),
            g_SSSDiffuseLut.GetSRV(),
            g_SSSSpecularLut.GetSRV(),
            g_Emu.GetSRV(),
            g_Eavg.GetSRV(),
        };
        UINT srcSizes[10] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
        UINT destSize = 10;
        D3D12_CPU_DESCRIPTOR_HANDLE destRange = m_CommonTextures;
        g_Device->CopyDescriptors(
            1,
            &destRange,
            &destSize,
            10,
            src,
            srcSizes,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}

