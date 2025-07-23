#include "pch.h"
#include "Renderer.h"
#include "TextureManager.h"
#include "GraphicsCore.h"
#include "DescriptorHeap.h"
#include "BufferManager.h"
#include "RootSignature.h"
#include "GraphicsCommon.h"
#include "Display.h"
#include "PipelineState.h"

#include "../CompiledShaders/PBRShadingVS.h"
#include "../CompiledShaders/SkyBoxVS.h"
#include "../CompiledShaders/ShadowVS.h"
#include "../CompiledShaders/DrawNormalsVS.h"
#include "../CompiledShaders/PostProcessVS.h"
#include "../CompiledShaders/BloomVS.h"
#include "../CompiledShaders/PBRShadingPS.h"
#include "../CompiledShaders/SkyBoxPS.h"
#include "../CompiledShaders/ShadowPS.h"
#include "../CompiledShaders/DrawNormalsPS.h"
#include "../CompiledShaders/PostProcessPS.h"
#include "../CompiledShaders/BloomPS.h"


using namespace TextureManager;
using namespace Graphics;
using namespace Microsoft::WRL;

namespace Renderer
{
	DescriptorHeap s_TextureHeap;
	DescriptorHandle m_CommonTextures;
	DescriptorHandle g_SSAOSrvHeap;
	DescriptorHandle g_SSAOUavHeap;
	DescriptorHandle g_PostprocessHeap;
	DescriptorHandle g_NullDescriptor;

	RootSignature s_RootSig;
	std::unordered_map<std::string, GraphicsPSO> s_PSOs;
	GraphicsPSO s_SkyboxPSO;


	void Initialize(void)
	{
		s_RootSig.Reset(kNumRootBindings, 5);
		s_RootSig.InitStaticSampler(0, SamplerLinearWrapDesc, D3D12_SHADER_VISIBILITY_PIXEL);
		s_RootSig.InitStaticSampler(1, SamplerLinearClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
		s_RootSig.InitStaticSampler(2, SamplerAnisotropicWrapDesc, D3D12_SHADER_VISIBILITY_PIXEL);
		s_RootSig.InitStaticSampler(3, SamplerAnisotropicClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
		s_RootSig.InitStaticSampler(4, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
		s_RootSig[kMeshConstants].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
		s_RootSig[kMaterialConstants].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
		s_RootSig[kMaterialSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10, D3D12_SHADER_VISIBILITY_PIXEL);
		s_RootSig[kCommonSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 10, D3D12_SHADER_VISIBILITY_PIXEL);
		s_RootSig[kCommonCBV].InitAsConstantBuffer(1);
		s_RootSig[kPostprocessSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 20, 10);
		s_RootSig[kShaderParams].InitAsConstantBuffer(2);
		s_RootSig.Finalize(L"RootSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();
		DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();
		DXGI_FORMAT NormalFormat = g_SceneNormalBuffer.GetFormat();


		D3D12_INPUT_ELEMENT_DESC DefaultInputLayout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		// Default PSO

		GraphicsPSO defaultPSO(L"Renderer::opaque PSO");
		defaultPSO.SetRootSignature(s_RootSig);
		defaultPSO.SetRasterizerState(RasterizerDefault);
		defaultPSO.SetBlendState(BlendNoColorWrite);
		defaultPSO.SetDepthStencilState(DepthStateReadWrite);
		defaultPSO.SetInputLayout(_countof(DefaultInputLayout), DefaultInputLayout);
		defaultPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		defaultPSO.SetRenderTargetFormats(1, &ColorFormat, DepthFormat);
		defaultPSO.SetVertexShader(g_pPBRShadingVS, sizeof(g_pPBRShadingVS));
		defaultPSO.SetPixelShader(g_pPBRShadingPS, sizeof(g_pPBRShadingPS));
		defaultPSO.Finalize();
		s_PSOs["opaque"] = defaultPSO;

		// shadow map PSO

		GraphicsPSO shadowPSO(L"Renderer::shadow PSO");
		shadowPSO = defaultPSO;
		shadowPSO.SetRasterizerState(RasterizerShadow);
		shadowPSO.SetRenderTargetFormats(0, nullptr, g_ShadowBuffer.GetFormat());
		shadowPSO.SetVertexShader(g_pShadowVS, sizeof(g_pShadowVS));
		shadowPSO.SetPixelShader(g_pShadowPS, sizeof(g_pShadowPS));
		shadowPSO.Finalize();
		s_PSOs["shadow"] = shadowPSO;

		// draw normal PSO

		GraphicsPSO drawNormalPSO(L"Renderer::drawNormal PSO");
		drawNormalPSO = defaultPSO;
		drawNormalPSO.SetRenderTargetFormats(1, &NormalFormat, DepthFormat);
		drawNormalPSO.SetVertexShader(g_pDrawNormalsVS, sizeof(g_pDrawNormalsVS));
		drawNormalPSO.SetPixelShader(g_pDrawNormalsPS, sizeof(g_pDrawNormalsPS));
		drawNormalPSO.Finalize();
		s_PSOs["drawNormals"] = drawNormalPSO;


		// PSO for sky.
		
		s_SkyboxPSO = defaultPSO;
		s_SkyboxPSO.SetRasterizerState(RasterizerTwoSided);
		s_SkyboxPSO.SetDepthStencilState(DepthStateReadOnly);
		s_SkyboxPSO.SetVertexShader(g_pSkyBoxVS, sizeof(g_pSkyBoxVS));
		s_SkyboxPSO.SetPixelShader(g_pSkyBoxPS, sizeof(g_pSkyBoxPS));
		s_SkyboxPSO.Finalize();


		// PSO for post process.

		GraphicsPSO postprocessPSO(L"Renderer::postprocess PSO");
		postprocessPSO = defaultPSO;
		postprocessPSO.SetRasterizerState(RasterizerTwoSided);
		postprocessPSO.SetDepthStencilState(DepthStateReadOnly);
		postprocessPSO.SetVertexShader(g_pPostProcessVS, sizeof(g_pPostProcessVS));
		postprocessPSO.SetPixelShader(g_pPostProcessPS, sizeof(g_pPostProcessPS));
		postprocessPSO.Finalize();
		s_PSOs["postprocess"] = postprocessPSO;


		TextureManager::Initialize(L"");

		s_TextureHeap.Create(L"Scene Texture Descriptors", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128);

		m_CommonTextures = s_TextureHeap.Alloc(9);
		g_PostprocessHeap = s_TextureHeap.Alloc();

		g_NullDescriptor = s_TextureHeap.Alloc();
		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.Texture2D.MipLevels = -1;
		desc.Texture2D.MostDetailedMip = 0;
		desc.Texture2D.ResourceMinLODClamp = 0;
		g_Device->CreateShaderResourceView(nullptr, &desc, g_NullDescriptor);

		uint32_t DestCount = 9;
		uint32_t SourceCounts[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1};


		D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
		{
			g_EnvirMap.GetSRV(),
			g_RadianceMap.GetSRV(),
			g_IrradianceMap.GetSRV(),
			g_SSAOFullScreen.GetSRV(),
			g_ShadowBuffer.GetDepthSRV(),
			g_LUT.GetSRV(),
			g_SSSLut.GetSRV(),
			g_Emu.GetSRV(),
			g_Eavg.GetSRV(),
		};

		g_Device->CopyDescriptors(1, &m_CommonTextures, &DestCount, DestCount, SourceTextures, SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		
		{
			g_SSAOSrvHeap = Renderer::s_TextureHeap.Alloc(4);
			g_SSAOUavHeap = Renderer::s_TextureHeap.Alloc();

			uint32_t DestCount = 4;
			uint32_t SourceCounts[] = { 1, 1, 1, 1};

			D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
			{
				g_SceneNormalBuffer.GetSRV(),
				g_SceneDepthBuffer.GetDepthSRV(),
				g_RandomVectorBuffer.GetSRV(),
				g_SSAOFullScreen.GetSRV(),
			};

			g_Device->CopyDescriptors(1, &g_SSAOSrvHeap, &DestCount, DestCount, SourceTextures,
				SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			g_Device->CopyDescriptorsSimple(1, g_SSAOUavHeap, g_SSAOFullScreen.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

	}
	void UpdateGlobalDescriptors(void)
	{
		uint32_t DestCount = 2;
		uint32_t SourceCounts[] = { 1, 1 };


		D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
		{
			g_SSAOFullScreen.GetSRV(),
			g_ShadowBuffer.GetDepthSRV(),
		};

		DescriptorHandle dest = m_CommonTextures + 3 * s_TextureHeap.GetDescriptorSize();

		g_Device->CopyDescriptors(1, &dest, &DestCount, DestCount, SourceTextures, SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	}
}