#include "pch.h"
#include "Renderer.h"
#include "TextureManager.h"
#include "GraphicsCore.h"
#include "DescriptorHeap.h"
#include "BufferManager.h"
#include "RootSignature.h"
#include "GraphicsCommon.h"
#include "Display.h"


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
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_PSOs;
	ComPtr<ID3D12PipelineState> s_SkyboxPSO;


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


		D3D12_INPUT_ELEMENT_DESC DefaultInputLayout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc = {};

		basePsoDesc.InputLayout = { DefaultInputLayout, _countof(DefaultInputLayout)};
		basePsoDesc.pRootSignature = s_RootSig.GetSignature();
		basePsoDesc.VS =
		{
			g_pPBRShadingVS,
			sizeof(g_pPBRShadingVS)
		};
		basePsoDesc.PS =
		{
			g_pPBRShadingPS,
			sizeof(g_pPBRShadingPS)
		};
		basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		basePsoDesc.BlendState.RenderTarget[0].BlendEnable = false;
		basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		basePsoDesc.SampleMask = UINT_MAX;
		basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		basePsoDesc.NumRenderTargets = 1;
		basePsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		basePsoDesc.SampleDesc.Count = 1;
		basePsoDesc.SampleDesc.Quality = 0;
		basePsoDesc.DSVFormat = DepthStencilFormat;

		//
		// PSO for opaque objects.
		//

		D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = basePsoDesc;
		//opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		opaquePsoDesc.DepthStencilState.DepthEnable = true;
		opaquePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		opaquePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		ThrowIfFailed(g_Device->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&m_PSOs["opaque"])));

		//
		// PSO for shadow map pass.
		//
		D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = basePsoDesc;
		smapPsoDesc.RasterizerState.DepthBias = 100000;
		smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
		smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
		smapPsoDesc.pRootSignature = s_RootSig.GetSignature();
		smapPsoDesc.VS =
		{
			g_pShadowVS,sizeof(g_pShadowVS)
		};
		smapPsoDesc.PS =
		{
			g_pShadowPS , sizeof(g_pShadowPS)
		};

		// Shadow map pass does not have a render target.
		smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
		smapPsoDesc.NumRenderTargets = 0;
		ThrowIfFailed(g_Device->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&m_PSOs["shadow_opaque"])));


		//
		// PSO for drawing normals.
		//
		D3D12_GRAPHICS_PIPELINE_STATE_DESC drawNormalsPsoDesc = basePsoDesc;
		drawNormalsPsoDesc.VS =
		{
			g_pDrawNormalsVS, sizeof(g_pDrawNormalsVS)
		};
		drawNormalsPsoDesc.PS =
		{
			g_pDrawNormalsPS, sizeof(g_pDrawNormalsPS)
		};
		drawNormalsPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		drawNormalsPsoDesc.SampleDesc.Count = 1;
		drawNormalsPsoDesc.SampleDesc.Quality = 0;
		drawNormalsPsoDesc.DSVFormat = DepthStencilFormat;
		ThrowIfFailed(g_Device->CreateGraphicsPipelineState(&drawNormalsPsoDesc, IID_PPV_ARGS(&m_PSOs["drawNormals"])));


		//
		// PSO for sky.
		//
		D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = basePsoDesc;
		skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

		skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		skyPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		skyPsoDesc.pRootSignature = s_RootSig.GetSignature();
		skyPsoDesc.VS =
		{
			g_pSkyBoxVS,sizeof(g_pSkyBoxVS)
		};
		skyPsoDesc.PS =
		{
			g_pSkyBoxPS,sizeof(g_pSkyBoxPS)
		};
		ThrowIfFailed(g_Device->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&s_SkyboxPSO)));

		//
		// PSO for post process.
		//
		D3D12_GRAPHICS_PIPELINE_STATE_DESC ppPsoDesc = basePsoDesc;
		ppPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		ppPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		ppPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		ppPsoDesc.pRootSignature = s_RootSig.GetSignature();
		ppPsoDesc.VS =
		{
			g_pPostProcessVS,sizeof(g_pPostProcessVS)
		};
		ppPsoDesc.PS =
		{
			g_pPostProcessPS,sizeof(g_pPostProcessPS)
		};
		ThrowIfFailed(g_Device->CreateGraphicsPipelineState(&ppPsoDesc, IID_PPV_ARGS(&m_PSOs["postprocess"])));


		D3D12_GRAPHICS_PIPELINE_STATE_DESC bloomPsoDesc = basePsoDesc;
		bloomPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		bloomPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		bloomPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		bloomPsoDesc.pRootSignature = s_RootSig.GetSignature();
		bloomPsoDesc.VS =
		{
			g_pBloomVS,sizeof(g_pBloomVS)
		};
		bloomPsoDesc.PS =
		{
			g_pBloomPS,sizeof(g_pBloomPS)
		};
		ThrowIfFailed(g_Device->CreateGraphicsPipelineState(&bloomPsoDesc, IID_PPV_ARGS(&m_PSOs["bloom"])));


		TextureManager::Initialize(L"");

		s_TextureHeap.Create(L"Scene Texture Descriptors", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128);

		m_CommonTextures = s_TextureHeap.Alloc(8);
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

		uint32_t DestCount = 8;
		uint32_t SourceCounts[] = { 1, 1, 1, 1, 1, 1, 1, 1};


		D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
		{
			g_EnvirMap.GetSRV(),
			g_RadianceMap.GetSRV(),
			g_IrradianceMap.GetSRV(),
			g_SSAOFullScreen.GetSRV(),
			g_ShadowBuffer.GetDepthSRV(),
			g_LUT.GetSRV(),
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