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
#include <cmath>
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
    for (auto& history : m_PostProcessTextures)
        for (auto& table : history) table = g_BindlessHeap.Alloc(4);
    m_SsaoPasses = std::make_unique<SSAO::ViewPasses>(SSAO::ViewResources{
        m_View.SceneDepth, m_View.SceneNormal, m_View.LinearDepthFull, m_View.LinearDepthHalf,
        m_View.SsaoFull, m_View.SsaoHalf, m_View.SsaoBlur });
    SetupGeometryGraph();
    SetupLightingGraph();
    SetupGiGraph();
    SetupPostProcessGraphs();
    UpdateViewDescriptors();
    m_Initialized = true;
}
void PbrRenderer::Shutdown()
{
    m_GeometryGraph.Clear();
    for (auto& graph : m_LightingGraphs) graph.Clear();
    m_GiGraph.Clear();
    for (auto& history : m_PostProcessGraphs)
        for (auto& graph : history) graph.Clear();

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

        const Vector3 rayDirection(m_LightPassGlobalConstants.SunDirection.x,
            m_LightPassGlobalConstants.SunDirection.y, m_LightPassGlobalConstants.SunDirection.z);
        // ShadowCamera uses positive view-Z depth and looks toward the light.
        // Place its near plane half a depth span before the volume center.
        m_SunShadowCamera.UpdateMatrix(-rayDirection,
            m_ShadowCenter - rayDirection * m_ShadowBounds.GetZ() * 0.5f, m_ShadowBounds,
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
        if (!m_LightCastsShadows || !m_ShaderAttribs.UseShadow) return;

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
	for (auto& graph : m_LightingGraphs) graph.Clear();
	auto render = [this](CommandContext& commandContext)
	{
		auto& context = commandContext.GetGraphicsContext();
		context.SetViewportAndScissor(m_View.Viewport, m_View.Scissor);
		context.SetRootSignature(m_Resources.Pipelines.GraphicsRoot);
		context.SetPipelineState(m_Resources.Pipelines.Graphics.at(m_GiSettings.Enabled ? "opaqueGi" : "opaque"));
		context.GetCommandList()->SetGraphicsRootDescriptorTable(Renderer::kCommonSRVs, m_CommonTextures);
		context.ClearColor(m_View.SceneColor);
		if (m_GiSettings.Enabled)
		{
			context.ClearColor(m_View.GiDiffuseWeight);
			const D3D12_CPU_DESCRIPTOR_HANDLE targets[] = { m_View.SceneColor.GetRTV(), m_View.GiDiffuseWeight.GetRTV() };
			context.SetRenderTargets(2, targets, m_View.SceneDepth.GetDSV());
		}
		else context.SetRenderTarget(m_View.SceneColor.GetRTV(), m_View.SceneDepth.GetDSV());
		context.SetDynamicConstantBufferView(kCommonCBV, sizeof(GlobalConstants), &m_LightPassGlobalConstants);
		auto shading = m_ShaderAttribs;
        shading.UseShadow = shading.UseShadow && m_LightCastsShadows;
        context.SetDynamicConstantBufferView(kShaderParams, sizeof(ShaderParams), &shading);

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
		context.SetRenderTarget(m_View.SceneColor.GetRTV(), m_View.SceneDepth.GetDSV());
		m_Resources.Skybox.Draw(context, false);
	};
	m_LightingGraphs[0].AddPass(L"Lighting / Opaque and sky", {
		{ &m_View.SceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET, RenderGraph::Access::Write },
		{ &m_View.SceneDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, RenderGraph::Access::Write },
		{ &m_View.Shadow, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
		{ &m_View.SsaoFull, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read }
	}, render);
	m_LightingGraphs[1].AddPass(L"Lighting / GI source and sky", {
		{ &m_View.SceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET, RenderGraph::Access::Write },
		{ &m_View.GiDiffuseWeight, D3D12_RESOURCE_STATE_RENDER_TARGET, RenderGraph::Access::Write },
		{ &m_View.SceneDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, RenderGraph::Access::Write },
		{ &m_View.Shadow, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
		{ &m_View.SsaoFull, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read }
	}, render);
}

void PbrRenderer::SetupGiGraph()
{
    struct GiConstants
    {
        Math::Matrix4 Projection;
        Math::Matrix4 InverseProjection;
        XMFLOAT4 ScreenSize;
        XMFLOAT4 Trace;
        XMFLOAT4 Control;
    };
    m_GiGraph.Clear();
    m_GiGraph.AddPass(L"GI / Trace visible diffuse bounce", {
        { &m_View.SceneColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_View.SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_View.SceneNormal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_View.GiDiffuseWeight, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_View.GiHalf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write }
    }, [this](CommandContext& commandContext)
    {
        auto& compute = commandContext.GetComputeContext();
        const GiConstants constants{
            m_View.Camera.GetProjMatrix(), Math::Invert(m_View.Camera.GetProjMatrix()),
            XMFLOAT4(float(m_View.Width()), float(m_View.Height()), 1.0f / m_View.Width(), 1.0f / m_View.Height()),
            XMFLOAT4(m_GiSettings.Radius, m_GiSettings.Thickness, float(m_GiSettings.Rays), float(m_GiSettings.Steps)),
            XMFLOAT4(m_View.Jitter.x, m_View.Jitter.y, m_GiSettings.Intensity,
                float(m_ppAttribs.useTAA ? m_View.FrameIndex : 0))
        };
        const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
            m_View.SceneColor.GetSRV(), m_View.SceneDepth.GetDepthSRV(), m_View.SceneNormal.GetSRV(),
            m_View.GiDiffuseWeight.GetSRV(), m_View.SceneColor.GetSRV()
        };
        compute.SetRootSignature(m_Resources.Pipelines.GiRoot);
        compute.SetPipelineState(m_Resources.Pipelines.GiTrace);
        compute.SetDynamicDescriptors(0, 0, 5, srvs);
        compute.SetDynamicDescriptor(1, 0, m_View.GiHalf.GetUAV());
        compute.SetDynamicConstantBufferView(2, sizeof(constants), &constants);
        compute.Dispatch2D(m_View.GiHalf.GetWidth(), m_View.GiHalf.GetHeight(), 8, 8);
    });
    m_GiGraph.AddPass(L"GI / Bilateral composite", {
        { &m_View.SceneColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_View.SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_View.SceneNormal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_View.GiDiffuseWeight, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_View.GiHalf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
        { &m_View.GiComposite, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write }
    }, [this](CommandContext& commandContext)
    {
        auto& compute = commandContext.GetComputeContext();
        const GiConstants constants{
            m_View.Camera.GetProjMatrix(), Math::Invert(m_View.Camera.GetProjMatrix()),
            XMFLOAT4(float(m_View.Width()), float(m_View.Height()), 1.0f / m_View.Width(), 1.0f / m_View.Height()),
            XMFLOAT4(m_GiSettings.Radius, m_GiSettings.Thickness, float(m_GiSettings.Rays), float(m_GiSettings.Steps)),
            XMFLOAT4(m_View.Jitter.x, m_View.Jitter.y, m_GiSettings.Intensity,
                float(m_ppAttribs.useTAA ? m_View.FrameIndex : 0))
        };
        const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
            m_View.SceneColor.GetSRV(), m_View.SceneDepth.GetDepthSRV(), m_View.SceneNormal.GetSRV(),
            m_View.GiDiffuseWeight.GetSRV(), m_View.GiHalf.GetSRV()
        };
        compute.SetRootSignature(m_Resources.Pipelines.GiRoot);
        compute.SetPipelineState(m_Resources.Pipelines.GiComposite);
        compute.SetDynamicDescriptors(0, 0, 5, srvs);
        compute.SetDynamicDescriptor(1, 0, m_View.GiComposite.GetUAV());
        compute.SetDynamicConstantBufferView(2, sizeof(constants), &constants);
        compute.Dispatch2D(m_View.GiComposite.GetWidth(), m_View.GiComposite.GetHeight(), 8, 8);
    });
}

void PbrRenderer::SetupPostProcessGraphs()
{
	for (uint32_t historyReadIndex = 0; historyReadIndex < 2; ++historyReadIndex)
	for (uint32_t giVariant = 0; giVariant < 2; ++giVariant)
	{
		const uint32_t historyWriteIndex = (historyReadIndex + 1u) & 1u;
		RenderGraph& graph = m_PostProcessGraphs[historyReadIndex][giVariant];
		auto& source = giVariant ? m_View.GiComposite : m_View.SceneColor;
		graph.Clear();
		graph.AddPass(L"Post / Temporal resolve", {
			{ &source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &m_View.History[historyReadIndex], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &m_View.SceneDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &m_View.Output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write }
		}, [this, historyReadIndex, giVariant](CommandContext& commandContext)
		{
			auto& compute = commandContext.GetComputeContext();
			compute.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Graphics::g_BindlessHeap.GetHeapPointer());
			compute.SetRootSignature(m_Resources.Pipelines.ComputeRoot);
			compute.SetPipelineState(m_Resources.Pipelines.Post);
			compute.SetDescriptorTable(0, m_PostProcessTextures[historyReadIndex][giVariant]);
			compute.SetDescriptorTable(1, m_PostProcessTextures[historyReadIndex][giVariant] + Graphics::CbvSrvUavDescriptorSize * 3);
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
    for (uint32_t gi = 0; gi < 2; ++gi)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE post[] = { (gi && m_View.GiComposite.GetResource() ? m_View.GiComposite : m_View.SceneColor).GetSRV(), m_View.History[history].GetSRV(),
            m_View.SceneDepth.GetDepthSRV(), m_View.Output.GetUAV() };
        for (uint32_t index = 0; index < _countof(post); ++index)
            g_Device->CopyDescriptorsSimple(1, m_PostProcessTextures[history][gi] + index * CbvSrvUavDescriptorSize,
                post[index], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}
void PbrRenderer::UpdateGiPostDescriptors()
{
    // The GI tables still contain their initialization placeholders until the
    // first GI frame. Once published, they are never rewritten before a GPU-idle resize.
    for (uint32_t history = 0; history < 2; ++history)
        g_Device->CopyDescriptorsSimple(1, m_PostProcessTextures[history][1],
            m_View.GiComposite.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}
void PbrRenderer::ExtractSceneView()
{
    const auto source = m_View.SceneCameraId ? m_View.SceneCameraId : m_Scene.ActiveCameraId();
    const auto* camera = m_View.UseSceneCamera ? m_Scene.FindCamera(source) : nullptr;
    const EntityId cameraId = camera ? camera->Id : InvalidEntityId;
    if (camera)
    {
        m_View.Camera.SetTransform(camera->Transform.transform);
        m_View.Camera.SetPerspectiveMatrix(DirectX::XMConvertToRadians(camera->Camera.VerticalFovDegrees),
            float(m_View.Height()) / std::max(1.0f, float(m_View.Width())), camera->Camera.NearClip, camera->Camera.FarClip);
        m_View.Camera.Update();
    }
    else m_View.Camera = m_View.EditorCamera;
    if (cameraId != m_ResolvedCamera) m_View.InvalidateHistory();
    m_ResolvedCamera = cameraId;
    m_ResolvedLight = InvalidEntityId;
    m_LightCastsShadows = false;
    m_LightPassGlobalConstants.SunDirection = { 0, 0, -1, 0 };
    m_LightPassGlobalConstants.SunRadiance = { 0, 0, 0, 0 };
    m_ShadowCenter = Vector3(0, 0, 0);
    m_ShadowBounds = Vector3(1, 1, 1);
    if (const auto* light = m_Scene.FindLight(m_Scene.ActiveLightId()); light && light->Light.Enabled)
    {
        const auto direction = Normalize(light->Transform.transform.GetRotation() * -Vector3(kZUnitVector));
        m_LightPassGlobalConstants.SunDirection = { direction.GetX(), direction.GetY(), direction.GetZ(), 0 };
        const auto& data = light->Light;
        m_LightPassGlobalConstants.SunRadiance = { data.Color.x * data.Intensity, data.Color.y * data.Intensity,
            data.Color.z * data.Intensity, 0 };
        m_ShadowCenter = Vector3(data.ShadowCenter.x, data.ShadowCenter.y, data.ShadowCenter.z);
        m_ShadowBounds = Vector3(data.ShadowBounds.x, data.ShadowBounds.y, data.ShadowBounds.z);
        m_ResolvedLight = light->Id;
        m_LightCastsShadows = data.CastsShadows;
    }
}

void PbrRenderer::Render(GraphicsContext& graphics)
{
    if (!m_Initialized || !m_Resources.Initialized()) throw std::logic_error("PBR view is not initialized.");
    auto finiteRange = [](float value, float fallback, float low, float high)
    {
        return std::clamp(std::isfinite(value) ? value : fallback, low, high);
    };
    m_GiSettings.Radius = finiteRange(m_GiSettings.Radius, 4.0f, 0.5f, 15.0f);
    m_GiSettings.Thickness = finiteRange(m_GiSettings.Thickness, 0.35f, 0.05f, 1.0f);
    m_GiSettings.Intensity = finiteRange(m_GiSettings.Intensity, 1.0f, 0.0f, 4.0f);
    m_GiSettings.Rays = std::clamp(m_GiSettings.Rays, 1u, 12u);
    m_GiSettings.Steps = std::clamp(m_GiSettings.Steps, 4u, 32u);
    const bool giEnabled = m_GiSettings.Enabled;
    if (giEnabled && !m_View.GiComposite.GetResource())
    {
        m_View.EnsureGiResources();
        UpdateGiPostDescriptors();
    }
    if (giEnabled != m_LastGiSettings.Enabled ||
        (giEnabled && (m_GiSettings.Radius != m_LastGiSettings.Radius ||
            m_GiSettings.Thickness != m_LastGiSettings.Thickness ||
            m_GiSettings.Intensity != m_LastGiSettings.Intensity ||
            m_GiSettings.Rays != m_LastGiSettings.Rays || m_GiSettings.Steps != m_LastGiSettings.Steps)))
    {
        m_View.InvalidateHistory();
    }
    m_LastGiSettings = m_GiSettings;
    ExtractSceneView();
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
	m_LightingGraphs[giEnabled ? 1 : 0].Execute(graphics);
	if (giEnabled) m_GiGraph.Execute(graphics);
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

	m_PostProcessGraphs[taaReadIndex][giEnabled ? 1 : 0].Execute(compute);

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
