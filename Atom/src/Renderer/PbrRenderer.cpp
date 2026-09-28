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
#include <cstring>

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
        m_View.SceneNormal.GetFormat(), m_View.Shadow.GetFormat(), m_View.Motion.GetFormat() }))
        throw std::logic_error("PBR view requires initialized resources with matching formats.");
    m_CommonTextures = g_BindlessHeap.Alloc(10);
    for (auto& table : m_PostProcessTextures) table = g_BindlessHeap.Alloc(8);
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
    m_PreviousModels.clear();
    m_ModelSnapshotSerial = 0;
    m_LastSceneRevision = UINT64_MAX;
    m_Initialized = false;
}

void PbrRenderer::SetupGeometryGraph()
{
	m_GeometryGraph.Clear();
	m_GeometryGraph.AddPass(L"Geometry / Depth and normals", {
		{ &m_View.SceneNormal, D3D12_RESOURCE_STATE_RENDER_TARGET, RenderGraph::Access::Write },
		{ &m_View.Motion, D3D12_RESOURCE_STATE_RENDER_TARGET, RenderGraph::Access::Write },
		{ &m_View.SceneDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, RenderGraph::Access::Write }
	}, [this](CommandContext& commandContext)
	{
		auto& context = commandContext.GetGraphicsContext();
		context.ClearColor(m_View.SceneNormal);
		context.ClearColor(m_View.Motion);
		context.ClearDepthAndStencil(m_View.SceneDepth);
		const D3D12_CPU_DESCRIPTOR_HANDLE targets[] = { m_View.SceneNormal.GetRTV(), m_View.Motion.GetRTV() };
		context.SetRenderTargets(2, targets, m_View.SceneDepth.GetDSV());
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
		m_LightPassGlobalConstants.InvScreenSize = {
			1.0f / m_View.Viewport.Width, 1.0f / m_View.Viewport.Height };
		context.SetDynamicConstantBufferView(kCommonCBV, sizeof(GlobalConstants), &m_LightPassGlobalConstants);

		for (auto& model : m_Scene.Models)
		{
			model.UpdateConstants();
			MotionMeshConstants constants;
			constants.ModelMatrix = model.Constants.ModelMatrix;
			constants.NormalMatrix = model.Constants.NormalMatrix;
			const auto previous = m_PreviousModels.find(model.Id);
			const bool valid = m_View.HistoryValid && previous != m_PreviousModels.end() &&
				previous->second.GPU == model.GPU.get();
			constants.PreviousModelViewProjection = m_View.PreviousViewProjection *
				(valid ? previous->second.Matrix : constants.ModelMatrix);
			constants.PreviousJitter = m_View.PreviousJitter;
			constants.Valid = valid ? 1u : 0u;
			context.SetDynamicConstantBufferView(kMeshConstants, sizeof(constants), &constants);
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
			{ &m_View.HistoryDepth[historyReadIndex], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &m_View.Motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &m_View.Output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write },
			{ &m_View.History[historyWriteIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write },
			{ &m_View.HistoryDepth[historyWriteIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RenderGraph::Access::Write }
		}, [this, historyReadIndex](CommandContext& commandContext)
		{
			auto& compute = commandContext.GetComputeContext();
			compute.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Graphics::g_BindlessHeap.GetHeapPointer());
			compute.SetRootSignature(m_Resources.Pipelines.ComputeRoot);
			compute.SetPipelineState(m_Resources.Pipelines.Post);
			compute.SetDescriptorTable(0, m_PostProcessTextures[historyReadIndex]);
			compute.SetDescriptorTable(1, m_PostProcessTextures[historyReadIndex] + Graphics::CbvSrvUavDescriptorSize * 5);
			compute.SetDynamicConstantBufferView(2, sizeof(PostProcess::RenderAttribs), &m_ppAttribs);
			compute.Dispatch((m_View.Output.GetWidth() + 7) / 8, (m_View.Output.GetHeight() + 7) / 8);
		});

		graph.AddPass(L"Post / Publish output and history", {
			{ &m_View.Output, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &m_View.History[historyWriteIndex], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, RenderGraph::Access::Read },
			{ &m_View.HistoryDepth[historyWriteIndex], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, RenderGraph::Access::Read }
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
        const uint32_t writeHistory = (history + 1u) & 1u;
        D3D12_CPU_DESCRIPTOR_HANDLE post[] = { m_View.SceneColor.GetSRV(), m_View.History[history].GetSRV(),
            m_View.SceneDepth.GetDepthSRV(), m_View.HistoryDepth[history].GetSRV(), m_View.Motion.GetSRV(),
            m_View.Output.GetUAV(),
            m_View.History[writeHistory].GetUAV(), m_View.HistoryDepth[writeHistory].GetUAV() };
        for (uint32_t index = 0; index < _countof(post); ++index)
            g_Device->CopyDescriptorsSimple(1, m_PostProcessTextures[history] + index * CbvSrvUavDescriptorSize,
                post[index], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
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
    ExtractSceneView();
    if (m_Scene.Revision() != m_LastSceneRevision)
    {
        // Preserve history for a transform-only edit so motion vectors can reproject it.
        bool transformOnly = m_Scene.Models.size() == m_PreviousModels.size();
        bool moved = false;
        for (const auto& model : m_Scene.Models)
        {
            const auto previous = m_PreviousModels.find(model.Id);
            if (previous == m_PreviousModels.end() || previous->second.GPU != model.GPU.get())
            {
                transformOnly = false;
                break;
            }
            const auto current = model.Transform.GetMatrix();
            moved |= std::memcmp(&current, &previous->second.Matrix, sizeof(current)) != 0;
        }
        if (!transformOnly || !moved) m_View.InvalidateHistory();
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

    ++m_ModelSnapshotSerial;
    for (const auto& model : m_Scene.Models)
    {
        auto& previous = m_PreviousModels[model.Id];
        previous.Matrix = model.Transform.GetMatrix();
        previous.GPU = model.GPU.get();
        previous.LastSeen = m_ModelSnapshotSerial;
    }
    for (auto previous = m_PreviousModels.begin(); previous != m_PreviousModels.end();)
        if (previous->second.LastSeen != m_ModelSnapshotSerial) previous = m_PreviousModels.erase(previous);
        else ++previous;
}
