#include "pch.h"
#include "Renderer.h"
#include "RootSignature.h"
#include "PbrRenderer.h"
#include "ConstantBuffers.h"
#include "GraphicsCommon.h"
#include "GraphicsCore.h"
#include "PipelineState.h"
#include "BufferManager.h"
#include "Display.h"
#include "FileSystem.h"

#include "Model.h"

namespace CS
{
#include "../CompiledShaders/SpecularBRDFCS.h"
#include "../CompiledShaders/IrradianceMapCS.h"
#include "../CompiledShaders/SpecularMapCS.h"
#include "../CompiledShaders/EquirectToCubeCS.h"
#include "../CompiledShaders/GenerateMipMapCS.h"
#include "../CompiledShaders/Emu.h"
#include "../CompiledShaders/Eavg.h"
#include "../CompiledShaders/PreIntegralDiffuseSSSCS.h"
#include "../CompiledShaders/PreIntegralSpecularSSSCS.h"

}

namespace
{
	std::unordered_map<std::string, ComputePSO> s_IBL_PSOCache;

    float Halton(uint32_t index, uint32_t base)
    {
        float f = 1.0f;
        float r = 0.0f;
        while (index > 0)
        {
            f /= static_cast<float>(base);
            r += f * static_cast<float>(index % base);
            index /= base;
        }
        return r;
    }

}

using namespace Graphics;
using namespace Renderer;
using namespace Microsoft::WRL;

using namespace CS;
using namespace DirectX;
using namespace DirectX::PackedVector;
PbrRenderer::PbrRenderer(Scene& scene) : m_Scene(scene)
{
}

void PbrRenderer::Initialize()
{
	Renderer::Initialize();

	m_Camera.SetEyeAtUp(Vector3(0.0f, 0.0f, -20.0f), Vector3(kZero), Vector3(kYUnitVector));
	m_Camera.SetZRange(m_CameraNearClip, m_CameraFarClip);
	m_Camera.SetAspectRatio(float(g_SceneColorBuffer.GetHeight()) / float(g_SceneColorBuffer.GetWidth()));
	m_CameraController.reset(new FlyingFPSCamera(m_Camera, Vector3(kYUnitVector)));

	g_IBLTexture = g_TextureManager.LoadHDR(FileSystem::GetFullPath(L"Assets/Textures/EnvirMap/sun.hdr"));

	PrecomputeCubemaps();

	m_SkyBox.model = ModelUtility::CreateModel(ModelUtility::LoadGltfAsset(FileSystem::GetFullPath("Assets/Models/cube.glb")));
	SetupGeometryGraph();
	SetupLightingGraph();
	SetupPostProcessGraphs();

	s_IBL_PSOCache.clear();

}

void PbrRenderer::Shutdown()
{
    m_GeometryGraph.Clear();
    m_LightingGraph.Clear();
    for (auto& graph : m_PostProcessGraphs)
        graph.Clear();
    m_SkyBox.model = Model{};
    m_CameraController.reset();
    Renderer::Shutdown();
}


void PbrRenderer::SetupGeometryGraph()
{
	m_GeometryGraph.Clear();
	m_GeometryGraph.AddPass(L"Geometry / Depth and normals", {
		{ &g_SceneNormalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, RenderGraph::Access::Write },
		{ &g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, RenderGraph::Access::Write }
	}, [this](CommandContext& commandContext)
	{
		auto& context = commandContext.GetGraphicsContext();
		context.ClearColor(g_SceneNormalBuffer);
		context.ClearDepthAndStencil(g_SceneDepthBuffer);
		context.SetRenderTarget(g_SceneNormalBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV());
		context.SetRootSignature(s_RootSig);
		context.SetPipelineState(s_PSOs["drawNormals"]);

		m_FrameJitter = { 0.0f, 0.0f };
		if (m_ppAttribs.useTAA != m_LastUseTAA)
		{
			m_TaaHistoryValid = false;
			m_FrameIndex = 0;
			m_LastUseTAA = m_ppAttribs.useTAA;
		}

		m_SunShadowCamera.UpdateMatrix(
			Vector3({ 15.0f, 15.5f, -15.0f }), Vector3({ 10, 10, -10 }), Vector3(5000, 3000, 3000),
			static_cast<uint32_t>(g_ShadowBuffer.GetWidth()), static_cast<uint32_t>(g_ShadowBuffer.GetHeight()), 16);

		if (m_ppAttribs.useTAA)
		{
			const float jitterX = Halton(static_cast<uint32_t>(m_FrameIndex & 1023u) + 1u, 2u) - 0.5f;
			const float jitterY = Halton(static_cast<uint32_t>(m_FrameIndex & 1023u) + 1u, 3u) - 0.5f;
			const float invW = 1.0f / std::max(1.0f, g_ViewPort.Width);
			const float invH = 1.0f / std::max(1.0f, g_ViewPort.Height);
			m_FrameJitter.x = jitterX * invW;
			m_FrameJitter.y = jitterY * invH;
		}

		m_LightPassGlobalConstants.ViewMatrix = m_Camera.GetViewMatrix();
		m_LightPassGlobalConstants.ProjMatrix = m_Camera.GetProjMatrix();
		m_LightPassGlobalConstants.ViewProjMatrix = m_Camera.GetViewProjMatrix();
		m_LightPassGlobalConstants.SunShadowMatrix = m_SunShadowCamera.GetShadowMatrix();
		m_LightPassGlobalConstants.CameraPos = m_Camera.GetPosition();
		m_LightPassGlobalConstants.SunPos = { 15, 15, -15 };
		m_LightPassGlobalConstants.Jitter = m_FrameJitter;
		context.SetDynamicConstantBufferView(kCommonCBV, sizeof(GlobalConstants), &m_LightPassGlobalConstants);

		for (auto& model : m_Scene.Models)
		{
			model.UpdateConstants();
			context.SetDynamicConstantBufferView(kMeshConstants, sizeof(MeshConstants), &model.Constants);
			model.Draw(context);
		}
	});

	m_GeometryGraph.AddPass(L"Shadow / Depth", {
		{ &g_ShadowBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, RenderGraph::Access::Write }
	}, [this](CommandContext& commandContext)
	{
		auto& context = commandContext.GetGraphicsContext();
		context.SetViewportAndScissor(0, 0, g_ShadowBuffer.GetWidth(), g_ShadowBuffer.GetHeight());
		context.ClearDepthAndStencil(g_ShadowBuffer);
		context.SetDepthStencilTarget(g_ShadowBuffer.GetDSV());

		m_ShadowPassGlobalConstants.ViewProjMatrix = m_SunShadowCamera.GetViewProjMatrix();
		m_ShadowPassGlobalConstants.Jitter = { 0.0f, 0.0f };
		context.SetRootSignature(s_RootSig);
		context.SetPipelineState(s_PSOs["shadow"]);
		context.SetDynamicConstantBufferView(kCommonCBV, sizeof(GlobalConstants), &m_ShadowPassGlobalConstants);

		for (auto& model : m_Scene.Models)
		{
			model.UpdateConstants();
			context.SetDynamicConstantBufferView(kMeshConstants, sizeof(MeshConstants), &model.Constants);
			model.Draw(context);
		}
	});

	m_GeometryGraph.AddPass(L"Shadow / Publish for lighting", {
		{ &g_ShadowBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read }
	}, {});
}


void PbrRenderer::SetupLightingGraph()
{
	m_LightingGraph.Clear();
	m_LightingGraph.AddPass(L"Lighting / Opaque and sky", {
		{ &g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, RenderGraph::Access::Write },
		{ &g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, RenderGraph::Access::Write },
		{ &g_ShadowBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
		{ &g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read }
	}, [this](CommandContext& commandContext)
	{
		auto& context = commandContext.GetGraphicsContext();
		context.SetViewportAndScissor(g_ViewPort, g_Rect);
		context.SetRootSignature(s_RootSig);
		context.SetPipelineState(s_PSOs["opaque"]);
		context.GetCommandList()->SetGraphicsRootDescriptorTable(Renderer::kCommonSRVs, Renderer::m_CommonTextures);
		context.ClearColor(g_SceneColorBuffer);
		context.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV());
		context.SetDynamicConstantBufferView(kCommonCBV, sizeof(GlobalConstants), &m_LightPassGlobalConstants);
		context.SetDynamicConstantBufferView(kShaderParams, sizeof(ShaderParams), &m_ShaderAttribs);

		for (auto& model : m_Scene.Models)
		{
			model.UpdateConstants();
			context.SetDynamicConstantBufferView(kMeshConstants, sizeof(MeshConstants), &model.Constants);
			model.Draw(context);
		}

		context.SetDynamicConstantBufferView(
			kMaterialConstants,
			sizeof(EnvMapRenderer::RenderAttribs),
			&m_EnvMapAttribs);
		context.SetRootSignature(s_RootSig);
		context.SetPipelineState(s_SkyboxPSO);
		m_SkyBox.model.Draw(context, false);
	});
}


void PbrRenderer::SetupPostProcessGraphs()
{
	for (uint32_t historyReadIndex = 0; historyReadIndex < 2; ++historyReadIndex)
	{
		const uint32_t historyWriteIndex = (historyReadIndex + 1u) & 1u;
		RenderGraph& graph = m_PostProcessGraphs[historyReadIndex];
		graph.Clear();
		graph.AddPass(L"Post / Temporal resolve", {
			{ &g_SceneColorBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &g_TAAHistoryBuffer[historyReadIndex], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &g_SceneDepthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &g_PostProcessBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write }
		}, [this, historyReadIndex](CommandContext& commandContext)
		{
			auto& compute = commandContext.GetComputeContext();
			compute.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Graphics::g_BindlessHeap.GetHeapPointer());
			compute.SetRootSignature(s_ComputeRootSig);
			compute.SetPipelineState(s_PostProcessPSO);
			g_Device->CopyDescriptorsSimple(1, g_PostProcessTexture, g_SceneColorBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			g_Device->CopyDescriptorsSimple(1, g_PostProcessTexture + Graphics::CbvSrvUavDescriptorSize, g_TAAHistoryBuffer[historyReadIndex].GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			g_Device->CopyDescriptorsSimple(1, g_PostProcessTexture + Graphics::CbvSrvUavDescriptorSize * 2, g_SceneDepthBuffer.GetDepthSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			g_Device->CopyDescriptorsSimple(1, g_PostProcessTexture + Graphics::CbvSrvUavDescriptorSize * 3, g_PostProcessBuffer.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			compute.SetDescriptorTable(0, g_PostProcessTexture);
			compute.SetDescriptorTable(1, g_PostProcessTexture + Graphics::CbvSrvUavDescriptorSize * 3);
			compute.SetDynamicConstantBufferView(2, sizeof(PostProcess::RenderAttribs), &m_ppAttribs);
			compute.Dispatch((g_PostProcessBuffer.GetWidth() + 7) / 8, (g_PostProcessBuffer.GetHeight() + 7) / 8);
		});

		graph.AddPass(L"Post / Store temporal history", {
			{ &g_PostProcessBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, RenderGraph::Access::Read },
			{ &g_TAAHistoryBuffer[historyWriteIndex], D3D12_RESOURCE_STATE_COPY_DEST, RenderGraph::Access::Write }
		}, [historyWriteIndex](CommandContext& commandContext)
		{
			auto& compute = commandContext.GetComputeContext();
			compute.GetCommandList()->CopyResource(
				g_TAAHistoryBuffer[historyWriteIndex].GetResource(),
				g_PostProcessBuffer.GetResource());
		});

		graph.AddPass(L"Post / Publish output and history", {
			{ &g_PostProcessBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &g_TAAHistoryBuffer[historyWriteIndex], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, RenderGraph::Access::Read }
		}, {});
	}
}

void PbrRenderer::OnResize()
{
    m_Camera.SetAspectRatio(float(g_SceneColorBuffer.GetHeight()) / float(g_SceneColorBuffer.GetWidth()));
    m_Camera.Update();
    m_TaaHistoryValid = false;
    m_FrameIndex = 0;
    m_PrevJitter = { 0.0f, 0.0f };
    m_PrevViewProj = m_Camera.GetViewProjMatrix();
}

void PbrRenderer::Update(float gt)
{
	Renderer::UpdateGlobalDescriptors();

    m_Camera.SetZRange(m_CameraNearClip, m_CameraFarClip);

	m_CameraController->Update(gt);
    m_Camera.Update();

}

void PbrRenderer::Render(GraphicsContext& graphics)
{
    auto& compute = graphics.GetComputeContext();
	graphics.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Graphics::g_BindlessHeap.GetHeapPointer());
	graphics.SetViewportAndScissor(g_ViewPort, g_Rect);

	// Geometry and shadow pass declarations own their output transitions.
	m_GeometryGraph.Execute(graphics);

	// ----------------------------------- Render SSAO --------------------------------
	SSAO::Render(graphics, m_Camera);

	// ------------------------------- Opaque and sky -------------------------------
	m_LightingGraph.Execute(graphics);
	graphics.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	graphics.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);


	// ------------------------- postprocess -----------------------
    const uint32_t taaReadIndex = m_TaaHistoryReadIndex & 1u;
    const uint32_t taaWriteIndex = (taaReadIndex + 1u) & 1u;
    m_ppAttribs.taaHistoryValid = m_ppAttribs.useTAA && m_TaaHistoryValid;
    m_ppAttribs.InvViewProj = Math::Invert(m_LightPassGlobalConstants.ViewProjMatrix);
    m_ppAttribs.PrevViewProj = m_TaaHistoryValid ? m_PrevViewProj : m_LightPassGlobalConstants.ViewProjMatrix;
    m_ppAttribs.CurrJitter = m_FrameJitter;
    m_ppAttribs.PrevJitter = m_TaaHistoryValid ? m_PrevJitter : m_FrameJitter;

	m_PostProcessGraphs[taaReadIndex].Execute(compute);

    if (m_ppAttribs.useTAA)
    {
        m_TaaHistoryReadIndex = taaWriteIndex;
        m_TaaHistoryValid = true;
        m_PrevViewProj = m_LightPassGlobalConstants.ViewProjMatrix;
        m_PrevJitter = m_FrameJitter;
        ++m_FrameIndex;
    }
    else
    {
        m_TaaHistoryValid = false;
        m_FrameIndex = 0;
        m_PrevViewProj = m_LightPassGlobalConstants.ViewProjMatrix;
        m_PrevJitter = { 0.0f, 0.0f };
    }
}

void PbrRenderer::PrecomputeCubemaps()
{
	ComputeContext& ComputeContext = ComputeContext::Begin(L"PrecomputeCubemaps");
    const auto CeilDiv = [](UINT value, UINT divisor) -> UINT
    {
        return (value + divisor - 1) / divisor;
    };
    const auto MipSize = [](UINT baseSize, UINT level) -> UINT
    {
        return std::max(1u, baseSize >> level);
    };

	__declspec(align(256)) struct ShaderParams
	{
		DWORD param0 = 0;
		DWORD param1 = 0;
	};
	ShaderParams shaderParams;
	s_IBL_PSOCache["EquirectToCube"].SetRootSignature(s_ComputeRootSig);
	s_IBL_PSOCache["EquirectToCube"].SetComputeShader(g_pEquirectToCubeCS, sizeof(g_pEquirectToCubeCS));
	s_IBL_PSOCache["EquirectToCube"].Finalize();

	s_IBL_PSOCache["GenerateMipMap"].SetRootSignature(s_ComputeRootSig);
	s_IBL_PSOCache["GenerateMipMap"].SetComputeShader(g_pGenerateMipMapCS, sizeof(g_pGenerateMipMapCS));
	s_IBL_PSOCache["GenerateMipMap"].Finalize();

	s_IBL_PSOCache["PrefilterSpecularMap"].SetRootSignature(s_ComputeRootSig);
	s_IBL_PSOCache["PrefilterSpecularMap"].SetComputeShader(g_pSpecularMapCS, sizeof(g_pSpecularMapCS));
	s_IBL_PSOCache["PrefilterSpecularMap"].Finalize();

	s_IBL_PSOCache["PrecomputeIrradianceMap"].SetRootSignature(s_ComputeRootSig);
	s_IBL_PSOCache["PrecomputeIrradianceMap"].SetComputeShader(g_pIrradianceMapCS, sizeof(g_pIrradianceMapCS));
	s_IBL_PSOCache["PrecomputeIrradianceMap"].Finalize();

	s_IBL_PSOCache["PrecomputeBRDF"].SetRootSignature(s_ComputeRootSig);
	s_IBL_PSOCache["PrecomputeBRDF"].SetComputeShader(g_pSpecularBRDFCS, sizeof(g_pSpecularBRDFCS));
	s_IBL_PSOCache["PrecomputeBRDF"].Finalize();

	s_IBL_PSOCache["emu"].SetRootSignature(s_ComputeRootSig);
	s_IBL_PSOCache["emu"].SetComputeShader(g_pEmu, sizeof(g_pEmu));
	s_IBL_PSOCache["emu"].Finalize();

	s_IBL_PSOCache["eavg"].SetRootSignature(s_ComputeRootSig);
	s_IBL_PSOCache["eavg"].SetComputeShader(g_pEavg, sizeof(g_pEavg));
	s_IBL_PSOCache["eavg"].Finalize();

	s_IBL_PSOCache["PreintegralDiffuseSSS"].SetRootSignature(s_ComputeRootSig);
	s_IBL_PSOCache["PreintegralDiffuseSSS"].SetComputeShader(g_pPreIntegralDiffuseSSSCS, sizeof(g_pPreIntegralDiffuseSSSCS));
	s_IBL_PSOCache["PreintegralDiffuseSSS"].Finalize();

	s_IBL_PSOCache["PreintegralSpecularSSS"].SetRootSignature(s_ComputeRootSig);
	s_IBL_PSOCache["PreintegralSpecularSSS"].SetComputeShader(g_pPreIntegralSpecularSSSCS, sizeof(g_pPreIntegralSpecularSSSCS));
	s_IBL_PSOCache["PreintegralSpecularSSS"].Finalize();

	{

		ComputeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Graphics::g_BindlessHeap.GetHeapPointer());

		ComputeContext.SetRootSignature(s_ComputeRootSig);
		ComputeContext.SetPipelineState(s_IBL_PSOCache["EquirectToCube"]);

		ComputeContext.TransitionResource(g_EnvirMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		ComputeContext.SetDynamicDescriptor(0, 0, g_TextureManager.Get(g_IBLTexture).GetSRV());
		ComputeContext.SetDynamicDescriptor(1, 0, g_EnvirMap.GetUAVArray()[0]);
		ComputeContext.SetDynamicConstantBufferView(2, sizeof(ShaderParams), &shaderParams);
		ComputeContext.Dispatch(CeilDiv(g_EnvirMap.GetWidth(), 32), CeilDiv(g_EnvirMap.GetHeight(), 32), 6);


		//// =======================  Generic MipMap
		ComputeContext.SetDynamicDescriptor(0, 0, g_EnvirMap.GetSRV());
		ComputeContext.SetPipelineState(s_IBL_PSOCache["GenerateMipMap"]);

        const UINT envMipLevels = g_EnvirMap.GetResource()->GetDesc().MipLevels;
		for (UINT level = 1; level < envMipLevels; ++level)
		{
			ComputeContext.SetDynamicDescriptor(1, 0, g_EnvirMap.GetUAVArray()[level]);

            const UINT mipWidth = MipSize(g_EnvirMap.GetWidth(), level);
            const UINT mipHeight = MipSize(g_EnvirMap.GetHeight(), level);
			shaderParams.param1 = level - 1;
			shaderParams.param0 = mipWidth;
			ComputeContext.SetDynamicConstantBufferView(2, sizeof(ShaderParams), &shaderParams);
			ComputeContext.Dispatch(CeilDiv(mipWidth, 32), CeilDiv(mipHeight, 32), 6);

			ComputeContext.TransitionResource(g_EnvirMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		ComputeContext.TransitionResource(g_EnvirMap, D3D12_RESOURCE_STATE_GENERIC_READ);
	}

	// Compute pre-filtered specular environment map
	{
		__declspec(align(256)) struct SpecularParameters
		{
			float Roughness = 0.0f;
		};
		SpecularParameters specularParameters;
		ComputeContext.SetPipelineState(s_IBL_PSOCache["PrefilterSpecularMap"]);
		ComputeContext.SetDynamicDescriptor(0, 0, g_EnvirMap.GetSRV());
		ComputeContext.TransitionResource(g_RadianceMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);


		const UINT levels = g_RadianceMap.GetResource()->GetDesc().MipLevels;
		const float deltaRoughness = 1.0f / std::max(float(levels - 1), (float)1);
		for (UINT level = 0; level < levels; ++level)
		{
            const UINT mipWidth = MipSize(g_RadianceMap.GetWidth(), level);
            const UINT mipHeight = MipSize(g_RadianceMap.GetHeight(), level);
			const float spmapRoughness = level * deltaRoughness;
			ComputeContext.SetDynamicDescriptor(1, 0, g_RadianceMap.GetUAVArray()[level]);
			specularParameters.Roughness = spmapRoughness;
			ComputeContext.SetDynamicConstantBufferView(2, sizeof(SpecularParameters), &specularParameters);
			ComputeContext.Dispatch(CeilDiv(mipWidth, 32), CeilDiv(mipHeight, 32), 6);

			ComputeContext.TransitionResource(g_RadianceMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		ComputeContext.TransitionResource(g_RadianceMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	}

	// create irradiance map compute pso
	{

		ComputeContext.SetPipelineState(s_IBL_PSOCache["PrecomputeIrradianceMap"]);


		ComputeContext.SetDynamicDescriptor(0, 0, g_EnvirMap.GetSRV());
		ComputeContext.SetDynamicDescriptor(1, 0, g_IrradianceMap.GetUAV());
		ComputeContext.TransitionResource(g_IrradianceMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		ComputeContext.Dispatch(CeilDiv(g_IrradianceMap.GetWidth(), 32), CeilDiv(g_IrradianceMap.GetHeight(), 32), 6);
		ComputeContext.TransitionResource(g_IrradianceMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}


	{

		ComputeContext.SetPipelineState(s_IBL_PSOCache["PrecomputeBRDF"]);

		ComputeContext.SetDynamicDescriptor(1, 0, g_LUT.GetUAV());
		ComputeContext.TransitionResource(g_LUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		ComputeContext.Dispatch(CeilDiv(g_LUT.GetWidth(), 32), CeilDiv(g_LUT.GetHeight(), 32), 1);
		ComputeContext.TransitionResource(g_LUT, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	}

	{

		ComputeContext.SetPipelineState(s_IBL_PSOCache["emu"]);
		ComputeContext.SetDynamicDescriptor(1, 0, g_Emu.GetUAV());
		ComputeContext.TransitionResource(g_Emu, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ComputeContext.Dispatch(CeilDiv(g_Emu.GetWidth(), 32), CeilDiv(g_Emu.GetHeight(), 32), 1);
		ComputeContext.TransitionResource(g_Emu, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	{
		ComputeContext.SetPipelineState(s_IBL_PSOCache["eavg"]);

		ComputeContext.SetDynamicDescriptor(1, 0, g_Eavg.GetUAV());
		ComputeContext.TransitionResource(g_Eavg, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ComputeContext.Dispatch(CeilDiv(g_Eavg.GetWidth(), 32), CeilDiv(g_Eavg.GetHeight(), 32), 1);
		ComputeContext.TransitionResource(g_Eavg, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	{
		ComputeContext.SetPipelineState(s_IBL_PSOCache["PreintegralDiffuseSSS"]);

		ComputeContext.SetDynamicDescriptor(1, 0, g_SSSDiffuseLut.GetUAV());
		ComputeContext.TransitionResource(g_SSSDiffuseLut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ComputeContext.Dispatch(CeilDiv(g_SSSDiffuseLut.GetWidth(), 32), CeilDiv(g_SSSDiffuseLut.GetHeight(), 32), 1);
		ComputeContext.TransitionResource(g_SSSDiffuseLut, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	{
		ComputeContext.SetPipelineState(s_IBL_PSOCache["PreintegralSpecularSSS"]);

		ComputeContext.SetDynamicDescriptor(1, 0, g_SSSSpecularLut.GetUAV());
		ComputeContext.TransitionResource(g_SSSSpecularLut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ComputeContext.Dispatch(CeilDiv(g_SSSSpecularLut.GetWidth(), 32), CeilDiv(g_SSSSpecularLut.GetHeight(), 32), 1);
		ComputeContext.TransitionResource(g_SSSSpecularLut, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	ComputeContext.Finish(true);
}
