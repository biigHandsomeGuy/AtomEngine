#include "pch.h"
#include "Renderer.h"
#include "RootSignature.h"
#include "PbrRenderer.h"
#include "ConstantBuffers.h"
#include "GraphicsCommon.h"
#include "GraphicsCore.h"
#include "PipelineState.h"
#include <utility>
#include "stb_image/stb_image.h"
#include <Imgui/imgui.h>
#include <Imgui/backends/imgui_impl_dx12.h>
#include <Imgui/backends/imgui_impl_win32.h>
#include "Display.h"
#include "CommandListManager.h"
#include "BufferManager.h"
#include <dxcapi.h>
#include <d3d12shader.h>
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


}

using namespace Graphics;
using namespace Renderer;
using namespace Microsoft::WRL;

using namespace CS;
using namespace DirectX;
using namespace DirectX::PackedVector;
float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) 
{
	return GameCore::RunApplication(PbrRenderer(hInstance), L"ModelViewer", hInstance, nCmdShow);
}


PbrRenderer::PbrRenderer(HINSTANCE hInstance)
{    
	
}

PbrRenderer::~PbrRenderer()
{
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void PbrRenderer::Startup()
{
	Renderer::Initialize();

	CommandContext& gfxContext = CommandContext::Begin(L"Scene Startup");

	m_Camera.SetEyeAtUp(Vector3(0.0f, 0.0f, -20.0f), Vector3(kZero), Vector3(kYUnitVector));
	m_Camera.SetZRange(0.1f, 10000.0f);
	//m_Camera.SetAspectRatio((float)g_DisplayWidth / g_DisplayHeight);
	m_CameraController.reset(new FlyingFPSCamera(m_Camera, Vector3(kYUnitVector)));

	g_IBLTexture = TextureManager::LoadHdrFromFile(FileSystem::GetFullPath(L"Assets/Textures/EnvirMap/sun.hdr"));

	PrecomputeCubemaps(gfxContext);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
	//io.ConfigViewportsNoAutoMerge = true;
	//io.ConfigViewportsNoTaskBarIcon = true;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup scaling
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)
	io.ConfigDpiScaleFonts = true;          // [Experimental] Automatically overwrite style.FontScaleDpi in Begin() when Monitor DPI changes. This will scale fonts but _NOT_ scale sizes/padding for now.
	io.ConfigDpiScaleViewports = true;      // [Experimental] Scale Dear ImGui and Platform Windows when Monitor DPI changes.
	io.Fonts->AddFontDefault();
	io.Fonts->Build();

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(GameCore::g_hWnd);

	DescriptorHandle ImGuiHandle[3] = 
	{ Renderer::s_TextureHeap.Alloc(),
	Renderer::s_TextureHeap.Alloc(),
	Renderer::s_TextureHeap.Alloc(), };

	D3D12_CPU_DESCRIPTOR_HANDLE srv[3] = { AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
	AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
	AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV), };

	g_Device->CopyDescriptorsSimple(1, ImGuiHandle[0], srv[0], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	g_Device->CopyDescriptorsSimple(1, ImGuiHandle[1], srv[1], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	g_Device->CopyDescriptorsSimple(1, ImGuiHandle[2], srv[2], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	ImGui_ImplDX12_Init(g_Device, 3,
		DXGI_FORMAT_R16G16B16A16_FLOAT, Renderer::s_TextureHeap.GetHeapPointer(),
		*ImGuiHandle,
		*ImGuiHandle);

	m_BackBufferHandle[0] = Renderer::s_TextureHeap.Alloc();
	m_BackBufferHandle[1] = Renderer::s_TextureHeap.Alloc();
	m_BackBufferHandle[2] = Renderer::s_TextureHeap.Alloc();

	Model skyBox, pbrModel, pbrModel2;
	skyBox = LoadGltfModel(FileSystem::GetFullPath("Assets/Models/cube.glb"));
	pbrModel = LoadGltfModel(FileSystem::GetFullPath("Assets/Models/DamagedHelmet/DamagedHelmet.gltf"));
	//pbrModel.
	m_SkyBox.model = std::move(skyBox);
	m_Scene.Models.push_back(std::move(pbrModel));
	m_Scene.Models.push_back(std::move(pbrModel2));
	m_MaterialConstants.resize(m_Scene.Models.size());

	s_IBL_PSOCache.clear();

	gfxContext.Finish(true);

}



void PbrRenderer::OnResize()
{
	
}

void PbrRenderer::Update(float gt)
{
	Renderer::UpdateGlobalDescriptors();
	
	m_CameraController->Update(gt);

}

void PbrRenderer::RenderScene()
{
	
	GraphicsContext& GraphicsContext = GraphicsContext::Begin(L"Scene Render");
	ComputeContext& ComputeContext = ComputeContext::Begin(L"PrecomputeCubemaps");
	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	static bool opt_fullscreen = true;
	static bool dockspaceOpen = true;
	static bool opt_padding = false;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

	// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
	// because it would be confusing to have two docking targets within each others.
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	if (opt_fullscreen)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	}

	// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
	// and handle the pass-thru hole, so we ask Begin() to not render a background.
	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;

	// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
	// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
	// all active windows docked into it will lose their parent and become undocked.
	// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
	// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);
	ImGui::PopStyleVar();

	if (opt_fullscreen)
		ImGui::PopStyleVar(2);

	// Submit the DockSpace
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}


	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{

			if (ImGui::MenuItem("Close", NULL, false, dockspaceOpen != NULL))
				dockspaceOpen = false;
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	
	static float f = 0.0f;

	ImGui::Begin("Inspector");
	XMVECTOR pos = m_Camera.GetPosition();
	XMFLOAT4 temp;
	XMStoreFloat4(&temp, pos);


	ImGui::Text("Camera Position: (%.3f, %.3f, %.3f, %.3f)", temp.x, temp.y, temp.z, temp.w);

	ImGui::SliderFloat("Env mip map", &m_EnvMapAttribs.EnvMapMipLevel, 0.0f, 10.0f);
	ImGui::SliderFloat("exposure", &m_ppAttribs.exposure, 0.1f, 5.0f);
	ImGui::Checkbox("UseFXAA", &m_ppAttribs.isRenderingLuminance);
	ImGui::Checkbox("UseReinhard", &m_ppAttribs.reinhard);
	ImGui::Checkbox("UseFilmic", &m_ppAttribs.filmic);
	ImGui::Checkbox("UseAces", &m_ppAttribs.aces);

	ImGui::Checkbox("UseSsao", &m_ShaderAttribs.UseSSAO);
	ImGui::Checkbox("UseShadow", &m_ShaderAttribs.UseShadow);
	ImGui::Checkbox("UseTexture", &m_ShaderAttribs.UseTexture);
	ImGui::Checkbox("UseEmu", &m_ShaderAttribs.UseEmu);
	ImGui::Checkbox("UseSSS", &m_ShaderAttribs.UseSSS);

	static struct
	{
		Vector3 Translate{ kIdentity };
		Vector3 Rotate{ kIdentity };
		Vector3 Scale{ kIdentity };
	} ModelMatrix;

	ImGui::DragFloat4("Translate", (float*)&ModelMatrix.Translate, 1.0f);
	ImGui::DragFloat4("Rotate", (float*)&ModelMatrix.Rotate, 1.0f);
	ImGui::DragFloat4("Scale", (float*)&ModelMatrix.Scale, 1.0f);

	m_Scene.Models[0].m_MeshComponent.Transform.SetTranslation(ModelMatrix.Translate);

	Quaternion q(
		XMConvertToRadians(ModelMatrix.Rotate.GetX()),
		XMConvertToRadians(ModelMatrix.Rotate.GetY()),
		XMConvertToRadians(ModelMatrix.Rotate.GetZ())
	);
	m_Scene.Models[0].m_MeshComponent.Transform.SetRotation(q);
	m_Scene.Models[0].m_MeshComponent.Scaling = ModelMatrix.Scale;
	if (m_ShaderAttribs.UseSSS)
	{
		ImGui::SliderFloat("CurveFactor", &m_ShaderAttribs.CurveFactor, 0, 10);
		ImGui::SliderFloat("SpecularFactor", &m_ShaderAttribs.SpecularFactor, 0, 1);
	}
	
	if (m_ShaderAttribs.UseTexture == false)
	{
		ImGui::ColorPicker3("albedo", m_ShaderAttribs.albedo);
		ImGui::SliderFloat("metallic", &m_ShaderAttribs.metallic, 0.0f, 1.0f);
		ImGui::SliderFloat("roughness", &m_ShaderAttribs.roughness, 0.0f, 1.0f);
	}


	UINT PrevBufferIndex = (g_CurrentBuffer + 3 - 1) % 3;


	GraphicsContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, s_TextureHeap.GetHeapPointer());
	GraphicsContext.SetViewportAndScissor(g_ViewPort, g_Rect);

	GraphicsContext.TransitionResource(g_DisplayPlane[g_CurrentBuffer], D3D12_RESOURCE_STATE_RENDER_TARGET);

	// ------------------------------------------ Z PrePass -------------------------------------------------

	GraphicsContext.TransitionResource(g_SceneNormalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
	GraphicsContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	GraphicsContext.ClearColor(g_SceneNormalBuffer);
	GraphicsContext.ClearDepthAndStencil(g_SceneDepthBuffer);

	GraphicsContext.SetRenderTarget(g_SceneNormalBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV());
	GraphicsContext.SetRootSignature(s_RootSig);
	GraphicsContext.SetPipelineState(s_PSOs["drawNormals"]);


	{
		m_SunShadowCamera.UpdateMatrix(Vector3({ 15.0f, 15.5f, -15.0f }), Vector3({10, 10, -10}), Vector3(5000, 3000, 3000),
			(uint32_t)g_ShadowBuffer.GetWidth(), (uint32_t)g_ShadowBuffer.GetHeight(), 16);

		m_LightPassGlobalConstants.ViewMatrix = m_Camera.GetViewMatrix();
		m_LightPassGlobalConstants.ProjMatrix = m_Camera.GetProjMatrix();
		m_LightPassGlobalConstants.ViewProjMatrix = m_Camera.GetViewProjMatrix();
		m_LightPassGlobalConstants.SunShadowMatrix = m_SunShadowCamera.GetShadowMatrix();
		m_LightPassGlobalConstants.CameraPos = m_Camera.GetPosition();
		m_LightPassGlobalConstants.SunPos = {15,15,-15};

	}
	GraphicsContext.SetDynamicConstantBufferView(kCommonCBV, sizeof(GlobalConstants), &m_LightPassGlobalConstants);


	for (int i = 0; i < m_Scene.Models.size(); i++)
	{
		m_Scene.Models[i].UpdateConstants();
		GraphicsContext.SetDynamicConstantBufferView(kMeshConstants, sizeof(MeshConstants), &m_Scene.Models[i].m_MeshConstants);

		m_Scene.Models[i].Draw(GraphicsContext.GetCommandList());
	}

	// --------------------------------- Shadow Map ----------------------------------
	

	GraphicsContext.TransitionResource(g_ShadowBuffer,D3D12_RESOURCE_STATE_DEPTH_WRITE);

	GraphicsContext.ClearDepthAndStencil(g_ShadowBuffer);

	GraphicsContext.SetDepthStencilTarget(g_ShadowBuffer.GetDSV());

	{
		
		m_ShadowPassGlobalConstants.ViewProjMatrix = m_SunShadowCamera.GetViewProjMatrix();
	}

	GraphicsContext.SetRootSignature(s_RootSig);
	GraphicsContext.SetPipelineState(s_PSOs["shadow"]);

	GraphicsContext.SetDynamicConstantBufferView(kCommonCBV, sizeof(GlobalConstants), &m_ShadowPassGlobalConstants);

	for (int i = 0; i < m_Scene.Models.size(); i++)
	{
		
		m_Scene.Models[i].UpdateConstants();

		GraphicsContext.SetDynamicConstantBufferView(kMeshConstants, sizeof(MeshConstants), &m_Scene.Models[i].m_MeshConstants);

		m_Scene.Models[i].Draw(GraphicsContext.GetCommandList());

	}

	GraphicsContext.TransitionResource(g_ShadowBuffer, D3D12_RESOURCE_STATE_COMMON);

	
	
	// ----------------------------------- Render SSAO --------------------------------
	SSAO::Render(GraphicsContext, m_Camera);

	// ----------------------------------- Render Color ------------------------------

	GraphicsContext.SetRootSignature(s_RootSig);
	GraphicsContext.SetPipelineState(s_PSOs["opaque"]);

	GraphicsContext.GetCommandList()->SetGraphicsRootDescriptorTable(Renderer::kCommonSRVs, Renderer::m_CommonTextures);
	
	GraphicsContext.GetCommandList()->RSSetViewports(1, &g_ViewPort);
	GraphicsContext.GetCommandList()->RSSetScissorRects(1, &g_Rect);


	GraphicsContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);

	GraphicsContext.ClearColor(g_SceneColorBuffer);
	GraphicsContext.ClearDepthAndStencil(g_SceneDepthBuffer);
	GraphicsContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV());

	GraphicsContext.SetDynamicConstantBufferView(kCommonCBV, sizeof(GlobalConstants), &m_LightPassGlobalConstants);

	GraphicsContext.SetDynamicConstantBufferView(kShaderParams, sizeof(ShaderParams), &m_ShaderAttribs);

	for (int i = 0; i < m_Scene.Models.size(); i++)
	{
		{
			m_MaterialConstants[i].gMatIndex = i;
		}
		{
			m_Scene.Models[i].UpdateConstants();
			Matrix4 T(
				{ 0.5f, 0.0f, 0.0f, 0.0f },
				{ 0.0f, -0.5f, 0.0f, 0.0f },
				{ 0.0f, 0.0f, 1.0f, 0.0f },
				{ 0.5f, 0.5f, 0.0f, 1.0f });
			Matrix4 viewProjTex = m_Camera.GetViewProjMatrix() * T;
			//m_MeshConstants[i].ViewProjTex = viewProjTex;

		}
		GraphicsContext.SetDynamicConstantBufferView(kMeshConstants, sizeof(MeshConstants), &m_Scene.Models[i].m_MeshConstants);
		GraphicsContext.SetDynamicConstantBufferView(kMaterialConstants, sizeof(MaterialConstants), &m_MaterialConstants[i]);

		m_Scene.Models[i].Draw(GraphicsContext.GetCommandList());
	}   


	GraphicsContext.SetDynamicConstantBufferView(kMaterialConstants, sizeof(MaterialConstants), &m_EnvMapAttribs);

	GraphicsContext.SetRootSignature(s_RootSig);
	GraphicsContext.SetPipelineState(s_SkyboxPSO);
	m_SkyBox.model.Draw(GraphicsContext.GetCommandList(), true);

	GraphicsContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);


	// ------------------------- postprocess -----------------------
	ComputeContext.TransitionResource(g_PostProcessBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	ComputeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Renderer::s_TextureHeap.GetHeapPointer());
	ComputeContext.SetRootSignature(s_ComputeRootSig);
	ComputeContext.SetPipelineState(s_PostProcessPSO);
	g_Device->CopyDescriptorsSimple(1, g_PostProcessTexture, g_SceneColorBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	g_Device->CopyDescriptorsSimple(1, g_PostProcessTexture + Graphics::CbvSrvUavDescriptorSize, g_PostProcessBuffer.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	ComputeContext.SetDescriptorTable(0, g_PostProcessTexture);
	ComputeContext.SetDescriptorTable(1, g_PostProcessTexture + Graphics::CbvSrvUavDescriptorSize);
	ComputeContext.SetDynamicConstantBufferView(2, sizeof(EnvMapRenderer::RenderAttribs), &m_ppAttribs);
	int width = g_PostProcessBuffer.GetWidth() / 8;
	int height = g_PostProcessBuffer.GetHeight() / 8;
	ComputeContext.Dispatch(width, height);
	ComputeContext.TransitionResource(g_PostProcessBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);


	ImGui::Text("GameCore average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

	ImGui::End();
	
	

	ImGui::Begin("ViewPort");

	ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();

	if (viewportPanelSize.x != g_RendererSize.x || viewportPanelSize.y != g_RendererSize.y)
	{
		g_RendererSize = { viewportPanelSize.x , viewportPanelSize.y };
		m_Camera.SetAspectRatio(g_RendererSize.y / g_RendererSize.x);
	}
	g_Device->CopyDescriptorsSimple(1, m_BackBufferHandle[g_CurrentBuffer], g_PostProcessBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	auto texID = (ImTextureID)m_BackBufferHandle[g_CurrentBuffer].GetGpuPtr();
	ImGui::Image(texID, viewportPanelSize);
	ImGui::End();
	

	ImGui::End();
	GraphicsContext.SetRenderTarget(g_DisplayPlane[g_CurrentBuffer].GetRTV(), g_SceneDepthBuffer.GetDSV());
	GraphicsContext.TransitionResource(
		g_DisplayPlane[g_CurrentBuffer],
		D3D12_RESOURCE_STATE_PRESENT);


	ImGui::Render();

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), GraphicsContext.GetCommandList());

	// Update and Render additional Platform Windows
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}

	GraphicsContext.Finish(true);
	ComputeContext.Finish(true);
}

void PbrRenderer::UpdateUI()
{
	

}

void PbrRenderer::PrecomputeCubemaps(CommandContext& gfxContext)
{
	ComputeContext& ComputeContext = ComputeContext::Begin(L"PrecomputeCubemaps");

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

		ComputeContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Renderer::s_TextureHeap.GetHeapPointer());

		ComputeContext.SetRootSignature(s_ComputeRootSig);
		ComputeContext.SetPipelineState(s_IBL_PSOCache["EquirectToCube"]);

		ComputeContext.TransitionResource(g_EnvirMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		ComputeContext.SetDynamicDescriptor(0, 0, g_IBLTexture.GetSRV());
		ComputeContext.SetDynamicDescriptor(1, 0, g_EnvirMap.GetUAVArray()[0]);
		ComputeContext.SetDynamicConstantBufferView(2, sizeof(ShaderParams), &shaderParams);
		ComputeContext.Dispatch(16, 16, 6);


		//// =======================  Generic MipMap
		ComputeContext.SetDynamicDescriptor(0, 0, g_EnvirMap.GetSRV());
		ComputeContext.SetPipelineState(s_IBL_PSOCache["GenerateMipMap"]);
	
		for (UINT level = 1, size = 256; level < 10; ++level, size /= 2)
		{
			ComputeContext.SetDynamicDescriptor(1, 0, g_EnvirMap.GetUAVArray()[level]);

			const UINT numGroups = std::max(1u, size / 32);
			shaderParams.param0 = size;
			shaderParams.param1 = level - 1;
			ComputeContext.SetDynamicConstantBufferView(2, sizeof(ShaderParams), &shaderParams);
			ComputeContext.Dispatch(numGroups, numGroups, 6);

			ComputeContext.TransitionResource(g_EnvirMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		ComputeContext.TransitionResource(g_EnvirMap, D3D12_RESOURCE_STATE_GENERIC_READ);
	}	
		
	// Compute pre-filtered specular environment map
	{	
		ComputeContext.SetPipelineState(s_IBL_PSOCache["PrefilterSpecularMap"]);
		ComputeContext.SetDynamicDescriptor(0, 0, g_EnvirMap.GetSRV());
		ComputeContext.TransitionResource(g_RadianceMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);


		const UINT levels = g_RadianceMap.GetResource()->GetDesc().MipLevels;
		const float deltaRoughness = 1.0f / std::max(float(levels - 1), (float)1);
		for (UINT level = 0, size = 256; level < levels; ++level, size /= 2)
		{
			const UINT numGroups = std::max<UINT>(1, size / 32);
			const float spmapRoughness = level * deltaRoughness;
			ComputeContext.SetDynamicDescriptor(1, 0, g_RadianceMap.GetUAVArray()[level]);
			shaderParams.param0 = spmapRoughness;
			ComputeContext.SetDynamicConstantBufferView(2, sizeof(ShaderParams), &shaderParams);
			ComputeContext.Dispatch(numGroups, numGroups, 6);

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

		ComputeContext.Dispatch(2, 2, 6);
		ComputeContext.TransitionResource(g_IrradianceMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
		
		
	{	
		
		ComputeContext.SetPipelineState(s_IBL_PSOCache["PrecomputeBRDF"]);
		
		ComputeContext.SetDynamicDescriptor(1, 0, g_LUT.GetUAV());
		ComputeContext.TransitionResource(g_LUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		ComputeContext.Dispatch(512/32, 512/32, 1);
		ComputeContext.TransitionResource(g_LUT, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	}	
		
	{	

		ComputeContext.SetPipelineState(s_IBL_PSOCache["emu"]);
		ComputeContext.SetDynamicDescriptor(1, 0, g_Emu.GetUAV());
		ComputeContext.TransitionResource(g_Emu, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ComputeContext.Dispatch(512 / 32, 512 / 32, 1);
		ComputeContext.TransitionResource(g_Emu, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}	
		
	{
		ComputeContext.SetPipelineState(s_IBL_PSOCache["eavg"]);

		ComputeContext.SetDynamicDescriptor(1, 0, g_Eavg.GetUAV());
		ComputeContext.TransitionResource(g_Eavg, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ComputeContext.Dispatch(512 / 32, 512 / 32, 1);
		ComputeContext.TransitionResource(g_Eavg, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	{
		ComputeContext.SetPipelineState(s_IBL_PSOCache["PreintegralDiffuseSSS"]);

		ComputeContext.SetDynamicDescriptor(1, 0, g_SSSDiffuseLut.GetUAV());
		ComputeContext.TransitionResource(g_SSSDiffuseLut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ComputeContext.Dispatch(512 / 32, 512 / 32, 1);
		ComputeContext.TransitionResource(g_SSSDiffuseLut, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	{
		ComputeContext.SetPipelineState(s_IBL_PSOCache["PreintegralSpecularSSS"]);

		ComputeContext.SetDynamicDescriptor(1, 0, g_SSSSpecularLut.GetUAV());
		ComputeContext.TransitionResource(g_SSSSpecularLut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ComputeContext.Dispatch(512 / 32, 512 / 32, 1);
		ComputeContext.TransitionResource(g_SSSSpecularLut, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	ComputeContext.Finish();
}