#include "pch.h"
#include "Renderer.h"
#include "RootSignature.h"
#include "PbrRenderer.h"
#include "ConstantBuffers.h"
#include "GraphicsCommon.h"
#include "GraphicsCore.h"
#include "PipelineState.h"
#include "BufferManager.h"
#include "CommandListManager.h"
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
PbrRenderer::PbrRenderer(Scene& scene, RenderView& view) : m_Scene(scene), m_View(view)
{
}

void PbrRenderer::Initialize()
{
    Renderer::Initialize(false, { m_View.SceneColor.GetFormat(), m_View.SceneDepth.GetFormat(),
        m_View.SceneNormal.GetFormat(), m_View.Shadow.GetFormat() });
    m_CommonTextures = g_BindlessHeap.Alloc(10);
    for (auto& table : m_PostProcessTextures) table = g_BindlessHeap.Alloc(4);
    m_SsaoPasses = std::make_unique<SSAO::ViewPasses>(SSAO::ViewResources{
        m_View.SceneDepth, m_View.SceneNormal, m_View.LinearDepthFull, m_View.LinearDepthHalf,
        m_View.SsaoFull, m_View.SsaoHalf, m_View.SsaoBlur });
    g_IBLTexture = g_TextureManager.LoadHDR(FileSystem::GetFullPath(L"Assets/Textures/EnvirMap/sun.hdr"));
    PrecomputeCubemaps();
    m_SkyBox.model = ModelUtility::CreateModel(ModelUtility::LoadGltfAsset(FileSystem::GetFullPath("Assets/Models/cube.glb")));
    SetupGeometryGraph();
    SetupLightingGraph();
    SetupPostProcessGraphs();
    UpdateViewDescriptors();
    s_IBL_PSOCache.clear();
}
void PbrRenderer::Shutdown()
{
    m_GeometryGraph.Clear();
    m_LightingGraph.Clear();
    for (auto& graph : m_PostProcessGraphs)
        graph.Clear();
    m_SkyBox.model = Model{};
    m_SsaoPasses.reset();
    Renderer::Shutdown();
}


void PbrRenderer::SetupGeometryGraph()
{
	m_GeometryGraph.Clear();
	m_GeometryGraph.AddPass(L"Geometry / Depth and normals", {
		{ &m_View.SceneNormal, D3D12_RESOURCE_STATE_RENDER_TARGET, RenderGraph::Access::Write },
		{ &m_View.SceneDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, RenderGraph::Access::Write }
	}, [this](CommandContext& commandContext)
	{
		auto& context = commandContext.GetGraphicsContext();
		context.ClearColor(m_View.SceneNormal);
		context.ClearDepthAndStencil(m_View.SceneDepth);
		context.SetRenderTarget(m_View.SceneNormal.GetRTV(), m_View.SceneDepth.GetDSV());
		context.SetRootSignature(s_RootSig);
		context.SetPipelineState(s_PSOs["drawNormals"]);

		m_View.Jitter = { 0.0f, 0.0f };
		if (m_ppAttribs.useTAA != m_View.LastUseTAA)
		{
			m_View.HistoryValid = false;
			m_View.FrameIndex = 0;
			m_View.LastUseTAA = m_ppAttribs.useTAA;
		}

		m_SunShadowCamera.UpdateMatrix(
			Vector3({ 15.0f, 15.5f, -15.0f }), Vector3({ 10, 10, -10 }), Vector3(5000, 3000, 3000),
			static_cast<uint32_t>(m_View.Shadow.GetWidth()), static_cast<uint32_t>(m_View.Shadow.GetHeight()), 16);

		if (m_ppAttribs.useTAA)
		{
			const float jitterX = Halton(static_cast<uint32_t>(m_View.FrameIndex & 1023u) + 1u, 2u) - 0.5f;
			const float jitterY = Halton(static_cast<uint32_t>(m_View.FrameIndex & 1023u) + 1u, 3u) - 0.5f;
			const float invW = 1.0f / std::max(1.0f, m_View.Viewport.Width);
			const float invH = 1.0f / std::max(1.0f, m_View.Viewport.Height);
			m_View.Jitter.x = jitterX * invW;
			m_View.Jitter.y = jitterY * invH;
		}

		m_LightPassGlobalConstants.ViewMatrix = m_View.Camera.GetViewMatrix();
		m_LightPassGlobalConstants.ProjMatrix = m_View.Camera.GetProjMatrix();
		m_LightPassGlobalConstants.ViewProjMatrix = m_View.Camera.GetViewProjMatrix();
		m_LightPassGlobalConstants.SunShadowMatrix = m_SunShadowCamera.GetShadowMatrix();
		m_LightPassGlobalConstants.CameraPos = m_View.Camera.GetPosition();
		m_LightPassGlobalConstants.SunPos = { 15, 15, -15 };
		m_LightPassGlobalConstants.Jitter = m_View.Jitter;
		context.SetDynamicConstantBufferView(kCommonCBV, sizeof(GlobalConstants), &m_LightPassGlobalConstants);

		for (auto& model : m_Scene.Models)
		{
			model.UpdateConstants();
			context.SetDynamicConstantBufferView(kMeshConstants, sizeof(MeshConstants), &model.Constants);
			model.Draw(context);
		}
	});

	m_GeometryGraph.AddPass(L"Shadow / Depth", {
		{ &m_View.Shadow, D3D12_RESOURCE_STATE_DEPTH_WRITE, RenderGraph::Access::Write }
	}, [this](CommandContext& commandContext)
	{
		auto& context = commandContext.GetGraphicsContext();
		context.SetViewportAndScissor(0, 0, m_View.Shadow.GetWidth(), m_View.Shadow.GetHeight());
		context.ClearDepthAndStencil(m_View.Shadow);
		context.SetDepthStencilTarget(m_View.Shadow.GetDSV());

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
		{ &m_View.Shadow, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read }
	}, {});
}


void PbrRenderer::SetupLightingGraph()
{
	m_LightingGraph.Clear();
	m_LightingGraph.AddPass(L"Lighting / Opaque and sky", {
		{ &m_View.SceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET, RenderGraph::Access::Write },
		{ &m_View.SceneDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, RenderGraph::Access::Write },
		{ &m_View.Shadow, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
		{ &m_View.SsaoFull, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read }
	}, [this](CommandContext& commandContext)
	{
		auto& context = commandContext.GetGraphicsContext();
		context.SetViewportAndScissor(m_View.Viewport, m_View.Scissor);
		context.SetRootSignature(s_RootSig);
		context.SetPipelineState(s_PSOs["opaque"]);
		context.GetCommandList()->SetGraphicsRootDescriptorTable(Renderer::kCommonSRVs, m_CommonTextures);
		context.ClearColor(m_View.SceneColor);
		context.SetRenderTarget(m_View.SceneColor.GetRTV(), m_View.SceneDepth.GetDSV());
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
			{ &m_View.SceneColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &m_View.History[historyReadIndex], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &m_View.SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &m_View.Output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write }
		}, [this, historyReadIndex](CommandContext& commandContext)
		{
			auto& compute = commandContext.GetComputeContext();
			compute.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Graphics::g_BindlessHeap.GetHeapPointer());
			compute.SetRootSignature(s_ComputeRootSig);
			compute.SetPipelineState(s_PostProcessPSO);
			compute.SetDescriptorTable(0, m_PostProcessTextures[historyReadIndex]);
			compute.SetDescriptorTable(1, m_PostProcessTextures[historyReadIndex] + Graphics::CbvSrvUavDescriptorSize * 3);
			compute.SetDynamicConstantBufferView(2, sizeof(PostProcess::RenderAttribs), &m_ppAttribs);
			compute.Dispatch((m_View.Output.GetWidth() + 7) / 8, (m_View.Output.GetHeight() + 7) / 8);
		});

		graph.AddPass(L"Post / Store temporal history", {
			{ &m_View.Output, D3D12_RESOURCE_STATE_COPY_SOURCE, RenderGraph::Access::Read },
			{ &m_View.History[historyWriteIndex], D3D12_RESOURCE_STATE_COPY_DEST, RenderGraph::Access::Write }
		}, [this, historyWriteIndex](CommandContext& commandContext)
		{
			auto& compute = commandContext.GetComputeContext();
			compute.GetCommandList()->CopyResource(
				m_View.History[historyWriteIndex].GetResource(),
				m_View.Output.GetResource());
		});

		graph.AddPass(L"Post / Publish output and history", {
			{ &m_View.Output, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &m_View.History[historyWriteIndex], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, RenderGraph::Access::Read }
		}, {});
	}
}

bool PbrRenderer::ResizeView(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return false;
    width = std::clamp(width, 8u, 16384u);
    height = std::clamp(height, 8u, 16384u);
    if (m_View.Width() == width && m_View.Height() == height)
        return false;
    g_CommandManager.IdleGPU();
    m_View.Resize(width, height);
    UpdateViewDescriptors();
    return true;
}

void PbrRenderer::UpdateViewDescriptors()
{
    D3D12_CPU_DESCRIPTOR_HANDLE source[] = {
        g_EnvirMap.GetSRV(), g_RadianceMap.GetSRV(), g_IrradianceMap.GetSRV(),
        m_View.SsaoFull.GetSRV(), m_View.Shadow.GetDepthSRV(), g_LUT.GetSRV(),
        g_SSSDiffuseLut.GetSRV(), g_SSSSpecularLut.GetSRV(), g_Emu.GetSRV(), g_Eavg.GetSRV()
    };
    for (uint32_t index = 0; index < _countof(source); ++index)
        g_Device->CopyDescriptorsSimple(1, m_CommonTextures + index * CbvSrvUavDescriptorSize,
            source[index], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // Immutable between view resizes; both history variants can be in flight.
    for (uint32_t history = 0; history < 2; ++history)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE post[] = { m_View.SceneColor.GetSRV(), m_View.History[history].GetSRV(),
            m_View.SceneDepth.GetDepthSRV(), m_View.Output.GetUAV() };
        for (uint32_t index = 0; index < _countof(post); ++index)
            g_Device->CopyDescriptorsSimple(1, m_PostProcessTextures[history] + index * CbvSrvUavDescriptorSize,
                post[index], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}
void PbrRenderer::Render(GraphicsContext& graphics)
{
    if (m_Scene.Revision() != m_LastSceneRevision)
    {
        m_View.InvalidateHistory();
        m_LastSceneRevision = m_Scene.Revision();
    }
    auto& compute = graphics.GetComputeContext();
	graphics.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Graphics::g_BindlessHeap.GetHeapPointer());
	graphics.SetViewportAndScissor(m_View.Viewport, m_View.Scissor);

	// Geometry and shadow pass declarations own their output transitions.
	m_GeometryGraph.Execute(graphics);

	// ----------------------------------- Render SSAO --------------------------------
	m_SsaoPasses->Render(graphics, m_View.Camera);

	// ------------------------------- Opaque and sky -------------------------------
	m_LightingGraph.Execute(graphics);
	graphics.TransitionResource(m_View.SceneColor, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	graphics.TransitionResource(m_View.SceneDepth, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);


	// ------------------------- postprocess -----------------------
    const uint32_t taaReadIndex = m_View.HistoryReadIndex & 1u;
    const uint32_t taaWriteIndex = (taaReadIndex + 1u) & 1u;
    m_ppAttribs.taaHistoryValid = m_ppAttribs.useTAA && m_View.HistoryValid;
    m_ppAttribs.InvViewProj = Math::Invert(m_LightPassGlobalConstants.ViewProjMatrix);
    m_ppAttribs.PrevViewProj = m_View.HistoryValid ? m_View.PreviousViewProjection : m_LightPassGlobalConstants.ViewProjMatrix;
    m_ppAttribs.CurrJitter = m_View.Jitter;
    m_ppAttribs.PrevJitter = m_View.HistoryValid ? m_View.PreviousJitter : m_View.Jitter;

	m_PostProcessGraphs[taaReadIndex].Execute(compute);

    if (m_ppAttribs.useTAA)
    {
        m_View.HistoryReadIndex = taaWriteIndex;
        m_View.HistoryValid = true;
        m_View.PreviousViewProjection = m_LightPassGlobalConstants.ViewProjMatrix;
        m_View.PreviousJitter = m_View.Jitter;
        ++m_View.FrameIndex;
    }
    else
    {
        m_View.HistoryValid = false;
        m_View.FrameIndex = 0;
        m_View.PreviousViewProjection = m_LightPassGlobalConstants.ViewProjMatrix;
        m_View.PreviousJitter = { 0.0f, 0.0f };
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
