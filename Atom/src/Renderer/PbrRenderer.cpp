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
#include <stdexcept>

#include "Model.h"

namespace
{

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

using namespace DirectX;
using namespace DirectX::PackedVector;
PbrRenderer::PbrRenderer(Scene& scene, RenderView& view, PbrResources& resources)
    : m_Scene(scene), m_View(view), m_Resources(resources)
{
}

void PbrRenderer::Initialize()
{
    if (m_Initialized) throw std::logic_error("PBR view is already initialized.");
    if (!m_Resources.Initialized() || !m_Resources.Pipelines.Matches({
        m_View.SceneColor.GetFormat(), m_View.SceneDepth.GetFormat(),
        m_View.SceneNormal.GetFormat(), m_View.Shadow.GetFormat() }))
        throw std::logic_error("PBR view requires initialized resources with matching formats.");
    m_CommonTextures = g_BindlessHeap.Alloc(10);
    for (auto& table : m_PostProcessTextures) table = g_BindlessHeap.Alloc(4);
    m_SsaoPasses = std::make_unique<SSAO::ViewPasses>(SSAO::ViewResources{
        m_View.SceneDepth, m_View.SceneNormal, m_View.LinearDepthFull, m_View.LinearDepthHalf,
        m_View.SsaoFull, m_View.SsaoHalf, m_View.SsaoBlur });
    SetupGeometryGraph();
    SetupLightingGraph();
    SetupPostProcessGraphs();
    UpdateViewDescriptors();
    m_Initialized = true;
}
void PbrRenderer::Shutdown()
{
    m_GeometryGraph.Clear();
    m_LightingGraph.Clear();
    for (auto& graph : m_PostProcessGraphs)
        graph.Clear();

    m_SsaoPasses.reset();
    m_Initialized = false;
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
		context.SetRootSignature(m_Resources.Pipelines.GraphicsRoot);
		context.SetPipelineState(m_Resources.Pipelines.Graphics.at("drawNormals"));

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
		context.SetRootSignature(m_Resources.Pipelines.GraphicsRoot);
		context.SetPipelineState(m_Resources.Pipelines.Graphics.at("shadow"));
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
		context.SetRootSignature(m_Resources.Pipelines.GraphicsRoot);
		context.SetPipelineState(m_Resources.Pipelines.Graphics.at("opaque"));
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
		context.SetRootSignature(m_Resources.Pipelines.GraphicsRoot);
		context.SetPipelineState(m_Resources.Pipelines.Skybox);
		m_Resources.Skybox.Draw(context, false);
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
			compute.SetRootSignature(m_Resources.Pipelines.ComputeRoot);
			compute.SetPipelineState(m_Resources.Pipelines.Post);
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
        m_Resources.Environment.GetSRV(), m_Resources.Radiance.GetSRV(), m_Resources.Irradiance.GetSRV(),
        m_View.SsaoFull.GetSRV(), m_View.Shadow.GetDepthSRV(), m_Resources.BrdfLut.GetSRV(),
        m_Resources.SssDiffuse.GetSRV(), m_Resources.SssSpecular.GetSRV(), m_Resources.Emu.GetSRV(), m_Resources.Eavg.GetSRV()
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
    if (!m_Initialized || !m_Resources.Initialized()) throw std::logic_error("PBR view is not initialized.");
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
