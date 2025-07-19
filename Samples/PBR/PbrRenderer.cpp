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
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx12.h"
#include "Display.h"
#include "CommandListManager.h"
#include "BufferManager.h"
#include <dxcapi.h>
#include <d3d12shader.h>



namespace CS
{
#include "../CompiledShaders/SpecularBRDFCS.h"
#include "../CompiledShaders/IrradianceMapCS.h"
#include "../CompiledShaders/SpecularMapCS.h"
#include "../CompiledShaders/EquirectToCubeCS.h"
#include "../CompiledShaders/GenerateMipMapCS.h"
#include "../CompiledShaders/Emu.h"
#include "../CompiledShaders/Eavg.h"

}

namespace
{
	RootSignature s_IBL_RootSig;
	std::unordered_map<std::string, ComputePSO> s_IBL_PSOCache;
}

using namespace Graphics;
using namespace Renderer;
using namespace GameCore;
using namespace Microsoft::WRL;

using namespace CS;
using namespace DirectX;
using namespace DirectX::PackedVector;

int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) 
{
#if defined(DEBUG) || defined(_DEBUG) 
	AllocConsole();

	FILE* fp;
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);
	freopen_s(&fp, "CONIN$", "r", stdin);
#endif

	return GameCore::RunApplication(PbrRenderer(hInstance), L"ModelViewer", hInstance, nCmdShow);
}

void ReflectDXIL(const void* dxilData, size_t dxilSize)
{
	ComPtr<IDxcContainerReflection> pReflection;
	if (FAILED(DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(&pReflection))))
	{
		std::cerr << "Failed to create DXC Container Reflection instance." << std::endl;
		return;
	}

	
	ComPtr<IDxcBlobEncoding> pBlob;
	ComPtr<IDxcLibrary> pLibrary;
	if (FAILED(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&pLibrary))))
	{
		std::cerr << "Failed to create DXC Library instance." << std::endl;
		return;
	}

	if (FAILED(pLibrary->CreateBlobWithEncodingFromPinned(dxilData, (UINT32)dxilSize, 0, &pBlob)))
	{
		std::cerr << "Failed to create DXC Blob from DXIL data." << std::endl;
		return;
	}

	// 2. 让 pReflection 解析 DXIL
	if (FAILED(pReflection->Load(pBlob.Get())))
	{
		std::cerr << "Failed to load DXIL reflection." << std::endl;
		return;
	}

	// 3. 查找 DXIL 代码部分
	UINT32 shaderIdx;
	if (FAILED(pReflection->FindFirstPartKind(DXC_PART_DXIL, &shaderIdx)))
	{
		std::cerr << "Failed to find DXIL part." << std::endl;
		return;
	}

	// 4. 获取 Shader Reflection
	ComPtr<ID3D12ShaderReflection> pShaderReflection;
	if (FAILED(pReflection->GetPartReflection(shaderIdx, IID_PPV_ARGS(&pShaderReflection))))
	{
		std::cerr << "Failed to get shader reflection." << std::endl;
		return;
	}

	// 5. 获取 Shader 描述信息
	D3D12_SHADER_DESC shaderDesc;
	if (FAILED(pShaderReflection->GetDesc(&shaderDesc)))
	{
		std::cerr << "Failed to get shader description." << std::endl;
		return;
	}

	std::cout << "着色器输入参数数量: " << shaderDesc.InputParameters << std::endl;
	std::cout << "着色器输出参数数量: " << shaderDesc.OutputParameters << std::endl;

	// 6. 遍历绑定资源
	for (UINT i = 0; i < shaderDesc.BoundResources; i++)
	{
		D3D12_SHADER_INPUT_BIND_DESC bindDesc;
		pShaderReflection->GetResourceBindingDesc(i, &bindDesc);
		std::cout << "资源绑定: " << bindDesc.Name << "，寄存器: " << bindDesc.BindPoint << std::endl;
	}
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

	m_Camera.SetPosition(0.0f, 2.0f, -5.0f);
	m_Camera.SetLens(0.25f * MathHelper::Pi, (float)g_DisplayWidth / g_DisplayHeight, 1.0f, 1000.0f);

	XMFLOAT4 pos{ 0.0f, 5.0f, 2.0f, 1.0f };
	XMVECTOR lightPos = XMLoadFloat4(&pos);
	XMStoreFloat4(&mLightPosW, lightPos);
	

	g_IBLTexture = TextureManager::LoadHdrFromFile(L"D:/AtomEngine/Atom/Assets/Textures/EnvirMap/sun.hdr");

	PrecomputeCubemaps(gfxContext);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
	auto size = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	DescriptorHandle ImGuiHandle[3] = { Renderer::s_TextureHeap.Alloc(),
	Renderer::s_TextureHeap.Alloc(), 
	Renderer::s_TextureHeap.Alloc(), };

	D3D12_CPU_DESCRIPTOR_HANDLE srv[3] = { AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
	AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV), 
	AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV), };

	g_Device->CopyDescriptorsSimple(1, ImGuiHandle[0], srv[0], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	g_Device->CopyDescriptorsSimple(1, ImGuiHandle[1], srv[1], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	g_Device->CopyDescriptorsSimple(1, ImGuiHandle[2], srv[2], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Setup Platform/PbrRenderer backends
	ImGui_ImplWin32_Init(g_hWnd);
	ImGui_ImplDX12_Init(g_Device, 3,
		DXGI_FORMAT_R16G16B16A16_FLOAT, Renderer::s_TextureHeap.GetHeapPointer(),
		ImGuiHandle[0],
		ImGuiHandle[0]);
	

	Model skyBox, pbrModel, pbrModel2;

	skyBox.Load(std::wstring(L"D:/AtomEngine/Atom/Assets/Models/cube.obj"), g_Device, gfxContext.GetCommandList());
	pbrModel.Load(std::wstring(L"D:/AtomEngine/Atom/Assets/Models/MaterialBall.obj"), g_Device, gfxContext.GetCommandList());
	//pbrModel2.Load(std::string("D:/AtomEngine/Atom/Assets/Models/plane.obj"), g_Device, gfxContext.GetCommandList());

	pbrModel.modelMatrix = XMMatrixRotationY(-80);
	pbrModel.modelMatrix *= XMMatrixScaling(0.3, 0.3, 0.3);
	pbrModel2.modelMatrix = XMMatrixScaling(4, 4, 4);
	pbrModel2.modelMatrix *= XMMatrixTranslation(0, -2, 0);

	pbrModel.normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, pbrModel.modelMatrix));
	pbrModel2.normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, pbrModel2.modelMatrix));

	m_SkyBox.model = std::move(skyBox);
	m_Scene.Models.push_back(std::move(pbrModel));
	//m_Scene.Models.push_back(std::move(pbrModel2));

	m_MeshConstants.resize(m_Scene.Models.size());
	m_MeshConstantsBuffers.resize(m_Scene.Models.size());
	m_MaterialConstants.resize(m_Scene.Models.size());
	m_MaterialConstantsBuffers.resize(m_Scene.Models.size());
	InitResource();
	mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	mSceneBounds.Radius = 15;

	s_IBL_PSOCache.clear();

	gfxContext.Finish(true);

}

void PbrRenderer::InitResource()
{
	
	shaderParamBufferSize = sizeof(ShaderParams);

	ThrowIfFailed(g_Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(shaderParamBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(shaderParamsCbuffer.GetAddressOf())
	));

	
	PostProcessBufferSize = sizeof(EnvMapRenderer::RenderAttribs);

	ThrowIfFailed(g_Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(PostProcessBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(ppBuffer.GetAddressOf())
	));

   
	EnvMapAttribsBufferSize = sizeof(EnvMapRenderer::RenderAttribs);

	ThrowIfFailed(g_Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(EnvMapAttribsBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(envMapBuffer.GetAddressOf())
	));


	ThrowIfFailed(g_Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(GlobalConstantsBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_LightPassGlobalConstantsBuffer.GetAddressOf())
	));

	ThrowIfFailed(g_Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(GlobalConstantsBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_ShadowPassGlobalConstantsBuffer.GetAddressOf())
	));

	for (int i = 0; i < m_Scene.Models.size(); i++)
	{
		ThrowIfFailed(g_Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(MeshConstantsBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(m_MeshConstantsBuffers[i].GetAddressOf())
		));
	}


	for (int i = 0; i < m_Scene.Models.size(); i++)
	{
		ThrowIfFailed(g_Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(MaterialConstantsBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(m_MaterialConstantsBuffers[i].GetAddressOf())
		));
	}
}


void PbrRenderer::OnResize()
{
	m_Camera.SetLens(0.25f*MathHelper::Pi, (float)g_DisplayWidth / g_DisplayHeight, 1.0f, 1000.0f);
}

void PbrRenderer::Update(float gt)
{
	UpdateUI();
	m_Camera.Update(gt);

	Renderer::UpdateGlobalDescriptors();
}


void PbrRenderer::RenderScene()
{
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

	ID3D12DescriptorHeap* descriptorHeaps[] = { s_TextureHeap.GetHeapPointer() };
	gfxContext.GetCommandList()->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	gfxContext.GetCommandList()->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(g_DisplayPlane[g_CurrentBuffer].GetResource(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	gfxContext.GetCommandList()->RSSetViewports(1, &g_ViewPort);
	gfxContext.GetCommandList()->RSSetScissorRects(1, &g_Rect);



	// ------------------------------------------ Z PrePass -------------------------------------------------

	
	
	gfxContext.GetCommandList()->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			g_SceneNormalBuffer.GetResource(),
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));

	gfxContext.GetCommandList()->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			g_SceneDepthBuffer.GetResource(),
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	// Clear the screen normal map and depth buffer.
	static XMFLOAT4 color = XMFLOAT4(0, 0, 0, 0);
	gfxContext.GetCommandList()->ClearRenderTargetView(
		g_SceneNormalBuffer.GetRTV(),
		&color.x, 0, nullptr);

	gfxContext.GetCommandList()->ClearDepthStencilView(
		g_SceneDepthBuffer.GetDSV(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	gfxContext.GetCommandList()->OMSetRenderTargets(1,
		&g_SceneNormalBuffer.GetRTV(),
		true, &g_SceneDepthBuffer.GetDSV());

	gfxContext.SetRootSignature(s_RootSig);
	gfxContext.SetPipelineState(s_PSOs["drawNormals"]);


	{
		BYTE* data = nullptr;
		m_LightPassGlobalConstantsBuffer->Map(0,
			nullptr, reinterpret_cast<void**>(&data));

		XMMATRIX view = m_Camera.GetView();
		XMMATRIX proj = m_Camera.GetProj();
		XMMATRIX viewProj = XMMatrixMultiply(view, proj);

		XMStoreFloat4x4(&m_LightPassGlobalConstants.ViewMatrix, view);
		XMStoreFloat4x4(&m_LightPassGlobalConstants.ProjMatrix, proj);
		XMStoreFloat4x4(&m_LightPassGlobalConstants.ViewProjMatrix, viewProj);
		m_LightPassGlobalConstants.SunShadowMatrix = mShadowTransform;
		m_LightPassGlobalConstants.CameraPos = m_Camera.GetPosition3f();
		m_LightPassGlobalConstants.SunPos = { mLightPosW.x,mLightPosW.y,mLightPosW.z };


		memcpy(data, &m_LightPassGlobalConstants, GlobalConstantsBufferSize);
		m_LightPassGlobalConstantsBuffer->Unmap(0, nullptr);
	}


	gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(
		kCommonCBV, m_LightPassGlobalConstantsBuffer->GetGPUVirtualAddress());

	for (int i = 0; i < m_Scene.Models.size(); i++)
	{
		{
			XMStoreFloat4x4(&m_MeshConstants[i].ModelMatrix, m_Scene.Models[i].modelMatrix);
			XMStoreFloat4x4(&m_MeshConstants[i].NormalMatrix, m_Scene.Models[i].normalMatrix);

			BYTE* data = nullptr;
			m_MeshConstantsBuffers[i]->Map(0, nullptr, reinterpret_cast<void**>(&data));

			memcpy(data, &m_MeshConstants[i].ModelMatrix, MeshConstantsBufferSize);
			m_MeshConstantsBuffers[i]->Unmap(0, nullptr);
		}
		gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(
			kMeshConstants, m_MeshConstantsBuffers[i]->GetGPUVirtualAddress());

		m_Scene.Models[i].Draw(gfxContext.GetCommandList());
	}
	gfxContext.GetCommandList()->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			g_SceneNormalBuffer.GetResource(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON));

	

	// --------------------------------- Shadow Map ----------------------------------
	

	gfxContext.GetCommandList()->ResourceBarrier(1, 
		&CD3DX12_RESOURCE_BARRIER::Transition(
			g_ShadowBuffer.GetResource(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));


	gfxContext.GetCommandList()->ClearDepthStencilView(
		g_ShadowBuffer.GetDSV(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 
		1.0f, 0, 0, nullptr);

	gfxContext.GetCommandList()->OMSetRenderTargets(
		0, nullptr, false, &g_ShadowBuffer.GetDSV());


	{
		XMVECTOR lightPos = XMLoadFloat4(&mLightPosW);
		XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
		targetPos = XMVectorSetW(targetPos, 1);
		XMVECTOR lightUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
		XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);
		XMStoreFloat4x4(&m_ShadowPassGlobalConstants.ViewMatrix, lightView);

		XMFLOAT3 sphereCenterLS;
		XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

		float l = sphereCenterLS.x - mSceneBounds.Radius;
		float b = sphereCenterLS.y - mSceneBounds.Radius;
		float n = sphereCenterLS.z - mSceneBounds.Radius;
		float r = sphereCenterLS.x + mSceneBounds.Radius;
		float t = sphereCenterLS.y + mSceneBounds.Radius;
		float f = sphereCenterLS.z + mSceneBounds.Radius;

		XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);
		XMStoreFloat4x4(&m_ShadowPassGlobalConstants.ProjMatrix, lightProj);


		BYTE* data = nullptr;
		ThrowIfFailed(m_ShadowPassGlobalConstantsBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data)));

		memcpy(data, &m_ShadowPassGlobalConstants, GlobalConstantsBufferSize);
		m_ShadowPassGlobalConstantsBuffer->Unmap(0, nullptr);
	}

	gfxContext.SetRootSignature(s_RootSig);
	gfxContext.SetPipelineState(s_PSOs["shadow"]);


	gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(
		kCommonCBV, m_ShadowPassGlobalConstantsBuffer->GetGPUVirtualAddress());

	for (int i = 0; i < m_Scene.Models.size(); i++)
	{
		{
			XMStoreFloat4x4(&m_MeshConstants[i].ModelMatrix, m_Scene.Models[i].modelMatrix);

			BYTE* data = nullptr;
			m_MeshConstantsBuffers[i]->Map(0, nullptr, reinterpret_cast<void**>(&data));

			memcpy(data, &m_MeshConstants[i].ModelMatrix, MeshConstantsBufferSize);
			m_MeshConstantsBuffers[i]->Unmap(0, nullptr);
		}

		gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(
			kMeshConstants, m_MeshConstantsBuffers[i]->GetGPUVirtualAddress());
		m_Scene.Models[i].Draw(gfxContext.GetCommandList());

	}

	gfxContext.GetCommandList()->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			g_ShadowBuffer.GetResource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COMMON));

	
	
	// ----------------------------------- Render SSAO --------------------------------
	SSAO::Render(gfxContext, m_Camera);

	// ----------------------------------- Render Color ------------------------------


	

	gfxContext.SetRootSignature(s_RootSig);
	gfxContext.SetPipelineState(s_PSOs["opaque"]);

	gfxContext.GetCommandList()->SetGraphicsRootDescriptorTable(Renderer::kCommonSRVs, Renderer::m_CommonTextures);
	
	gfxContext.GetCommandList()->RSSetViewports(1, &g_ViewPort);
	gfxContext.GetCommandList()->RSSetScissorRects(1, &g_Rect);


	gfxContext.GetCommandList()->ResourceBarrier(1, 
		&CD3DX12_RESOURCE_BARRIER::Transition(
			g_SceneColorBuffer.GetResource(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));


	gfxContext.GetCommandList()->ClearRenderTargetView(
		g_SceneColorBuffer.GetRTV(), &color.x, 0, nullptr);

	gfxContext.GetCommandList()->ClearDepthStencilView(
		g_SceneDepthBuffer.GetDSV(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	gfxContext.GetCommandList()->OMSetRenderTargets(1, 
		&g_SceneColorBuffer.GetRTV(), true, &g_SceneDepthBuffer.GetDSV());

	gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(
		kCommonCBV, m_LightPassGlobalConstantsBuffer->GetGPUVirtualAddress());

	
	{        
		BYTE* data = nullptr;
		shaderParamsCbuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
		//m_ShaderAttribs.roughness *= 0.5;
		memcpy(data, &m_ShaderAttribs, shaderParamBufferSize);
		shaderParamsCbuffer->Unmap(0, nullptr);
		
	}
	gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(
		kShaderParams, shaderParamsCbuffer->GetGPUVirtualAddress());

	for (int i = 0; i < m_Scene.Models.size(); i++)
	{
		{
			BYTE* data = nullptr;
			m_MaterialConstantsBuffers[i]->Map(0, nullptr, reinterpret_cast<void**>(&data));

			m_MaterialConstants[i].gMatIndex = i;
			memcpy(data, &m_MaterialConstants[i].gMatIndex, MaterialConstantsBufferSize);
			m_MaterialConstantsBuffers[i]->Unmap(0, nullptr);

		}
		{
			XMStoreFloat4x4(&m_MeshConstants[i].ModelMatrix, m_Scene.Models[i].modelMatrix);
			XMStoreFloat4x4(&m_MeshConstants[i].NormalMatrix, m_Scene.Models[i].normalMatrix);
			XMMATRIX T(
				0.5f, 0.0f, 0.0f, 0.0f,
				0.0f, -0.5f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.5f, 0.5f, 0.0f, 1.0f);
			XMMATRIX viewProjTex = m_Camera.GetView() * m_Camera.GetProj() * T;
			XMStoreFloat4x4(&m_MeshConstants[i].ViewProjTex, viewProjTex);

			BYTE* data = nullptr;
			m_MeshConstantsBuffers[i]->Map(0, nullptr, reinterpret_cast<void**>(&data));

			memcpy(data, &m_MeshConstants[i].ModelMatrix, MeshConstantsBufferSize);
			m_MeshConstantsBuffers[i]->Unmap(0, nullptr);
		}
		gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(
			kMeshConstants, m_MeshConstantsBuffers[i]->GetGPUVirtualAddress());
		gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(
			kMaterialConstants, m_MaterialConstantsBuffers[i]->GetGPUVirtualAddress());

		m_Scene.Models[i].Draw(gfxContext.GetCommandList());
	}   

	// ------------------------- skybox -----------------------
	{
		BYTE* data = nullptr;
		envMapBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));

		memcpy(data, &m_EnvMapAttribs, EnvMapAttribsBufferSize);
		envMapBuffer->Unmap(0, nullptr);
	}

	gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(kMaterialConstants, envMapBuffer->GetGPUVirtualAddress());
	gfxContext.SetRootSignature(s_RootSig);
	gfxContext.GetCommandList()->SetPipelineState(s_SkyboxPSO.GetPipelineStateObject());
	m_SkyBox.model.Draw(gfxContext.GetCommandList());

	gfxContext.GetCommandList()->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			g_SceneColorBuffer.GetResource(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE));


	// ------------------------- postprocess -----------------------
	
	gfxContext.GetCommandList()->OMSetRenderTargets(1,
		&g_DisplayPlane[g_CurrentBuffer].GetRTV(), true, &g_SceneDepthBuffer.GetDSV());

	
	{
		BYTE* data = nullptr;
		ppBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
	
		memcpy(data, &m_ppAttribs, PostProcessBufferSize);
		ppBuffer->Unmap(0, nullptr);
	}

	gfxContext.SetRootSignature(s_RootSig);
	gfxContext.SetPipelineState(s_PSOs["postprocess"]);


	gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(kMaterialConstants, ppBuffer->GetGPUVirtualAddress());

	g_Device->CopyDescriptorsSimple(1, g_PostprocessHeap, g_SceneColorBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	gfxContext.GetCommandList()->SetGraphicsRootDescriptorTable(Renderer::kCommonSRVs, Renderer::m_CommonTextures);

	gfxContext.GetCommandList()->SetGraphicsRootDescriptorTable(Renderer::kPostprocessSRVs, Renderer::g_PostprocessHeap);

	gfxContext.GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	
	gfxContext.GetCommandList()->DrawInstanced(4, 1, 0, 0);

	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gfxContext.GetCommandList());


	gfxContext.GetCommandList()->ResourceBarrier(1, 
		&CD3DX12_RESOURCE_BARRIER::Transition(
			g_SceneDepthBuffer.GetResource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COMMON));

	gfxContext.GetCommandList()->ResourceBarrier(1, 
		&CD3DX12_RESOURCE_BARRIER::Transition(
			g_DisplayPlane[g_CurrentBuffer].GetResource(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
	
	gfxContext.GetCommandList()->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			g_SceneColorBuffer.GetResource(),
			D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));


	gfxContext.Finish(true);
}



void PbrRenderer::UpdateUI()
{
	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	static bool show_demo_window = false;
	static bool show_another_window = false;
 
	if (show_demo_window)
		ImGui::ShowDemoWindow(&show_demo_window);
	static int b = 0;
	
	{
		static float f = 0.0f;
		static int counter = 0;

		ImGui::Begin("debug");                          // Create a window called "Hello, world!" and append into it.
 
		ImGui::Text("Welcome to my renderer!");               // Display some text (you can use a format strings too)
		ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
		ImGui::Checkbox("Another Window", &show_another_window);

		ImGui::SliderFloat("Env mip map", &m_EnvMapAttribs.EnvMapMipLevel, 0.0f, 10.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
		ImGui::SliderFloat("exposure", &m_ppAttribs.exposure, 0.1f, 5.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
		ImGui::Checkbox("UseFXAA", &m_ppAttribs.isRenderingLuminance);
		ImGui::Checkbox("UseReinhard", &m_ppAttribs.reinhard);
		ImGui::Checkbox("UseFilmic", &m_ppAttribs.filmic);
		ImGui::Checkbox("UseAces", &m_ppAttribs.aces);
	   
		
		ImGui::DragFloat4("LightPosition", &mLightPosW.x,0.3f,-90,90);
		ImGui::DragFloat4("CameraPosition", &(m_Camera.GetPosition3f().x));

		ImGui::Checkbox("UseSsao", &m_ShaderAttribs.UseSSAO);
		ImGui::Checkbox("UseShadow", &m_ShaderAttribs.UseShadow);       
		ImGui::Checkbox("UseTexture", &m_ShaderAttribs.UseTexture);       
		ImGui::Checkbox("UseEmu", &m_ShaderAttribs.UseEmu);       
		if (m_ShaderAttribs.UseTexture == false)
		{
			ImGui::ColorPicker3("albedo", m_ShaderAttribs.albedo);
			ImGui::SliderFloat("metallic", &m_ShaderAttribs.metallic, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
			ImGui::SliderFloat("roughness", &m_ShaderAttribs.roughness, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f

		   
		}
		if (m_ShaderAttribs.UseSSAO == true)
		{
			// // Coordinates given in view space.
			// ImGui::SliderFloat("OcclusionRadius",&ssaoCB.OcclusionRadius, 0.0f, 1.0f);
			// ImGui::SliderFloat("OcclusionFadeStart",&ssaoCB.OcclusionFadeStart, 0.0f, 1.0f);
			// ImGui::SliderFloat("OcclusionFadeEnd",&ssaoCB.OcclusionFadeEnd, 0.0f, 1.0f);
			// ImGui::SliderFloat("SurfaceEpsilon",&ssaoCB.SurfaceEpsilon, 0.0f, 1.0f);
		}

		

		if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
		  counter++;
		ImGui::SameLine();
		ImGui::Text("counter = %d", counter);



		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

		ImGui::Text("GameCore average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
		ImGui::End();
	}
}

void PbrRenderer::PrecomputeCubemaps(CommandContext& gfxContext)
{
	ComputeContext& GfxContext = ComputeContext::Begin(L"PrecomputeCubemaps");

	s_IBL_RootSig.Reset(4, 5);
	s_IBL_RootSig.InitStaticSampler(0, SamplerLinearWrapDesc);
	s_IBL_RootSig.InitStaticSampler(1, SamplerLinearClampDesc);
	s_IBL_RootSig.InitStaticSampler(2, SamplerAnisotropicWrapDesc);
	s_IBL_RootSig.InitStaticSampler(3, SamplerAnisotropicClampDesc);
	s_IBL_RootSig.InitStaticSampler(4, SamplerShadowDesc);
	s_IBL_RootSig[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
	s_IBL_RootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
	s_IBL_RootSig[2].InitAsConstants(0, 1);
	s_IBL_RootSig[3].InitAsConstants(1, 1);
	s_IBL_RootSig.Finalize(L"s_IBL_RootSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	s_IBL_PSOCache["EquirectToCube"].SetRootSignature(s_IBL_RootSig);
	s_IBL_PSOCache["EquirectToCube"].SetComputeShader(g_pEquirectToCubeCS, sizeof(g_pEquirectToCubeCS));
	s_IBL_PSOCache["EquirectToCube"].Finalize();

	s_IBL_PSOCache["GenerateMipMap"].SetRootSignature(s_IBL_RootSig);
	s_IBL_PSOCache["GenerateMipMap"].SetComputeShader(g_pGenerateMipMapCS, sizeof(g_pGenerateMipMapCS));
	s_IBL_PSOCache["GenerateMipMap"].Finalize();

	s_IBL_PSOCache["PrefilterSpecularMap"].SetRootSignature(s_IBL_RootSig);
	s_IBL_PSOCache["PrefilterSpecularMap"].SetComputeShader(g_pSpecularMapCS, sizeof(g_pSpecularMapCS));
	s_IBL_PSOCache["PrefilterSpecularMap"].Finalize();

	s_IBL_PSOCache["PrecomputeIrradianceMap"].SetRootSignature(s_IBL_RootSig);
	s_IBL_PSOCache["PrecomputeIrradianceMap"].SetComputeShader(g_pIrradianceMapCS, sizeof(g_pIrradianceMapCS));
	s_IBL_PSOCache["PrecomputeIrradianceMap"].Finalize();

	s_IBL_PSOCache["PrecomputeBRDF"].SetRootSignature(s_IBL_RootSig);
	s_IBL_PSOCache["PrecomputeBRDF"].SetComputeShader(g_pSpecularBRDFCS, sizeof(g_pSpecularBRDFCS));
	s_IBL_PSOCache["PrecomputeBRDF"].Finalize();

	s_IBL_PSOCache["emu"].SetRootSignature(s_IBL_RootSig);
	s_IBL_PSOCache["emu"].SetComputeShader(g_pEmu, sizeof(g_pEmu));
	s_IBL_PSOCache["emu"].Finalize();

	s_IBL_PSOCache["eavg"].SetRootSignature(s_IBL_RootSig);
	s_IBL_PSOCache["eavg"].SetComputeShader(g_pEavg, sizeof(g_pEavg));
	s_IBL_PSOCache["eavg"].Finalize();

	{

		GfxContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Renderer::s_TextureHeap.GetHeapPointer());

		GfxContext.SetRootSignature(s_IBL_RootSig);
		GfxContext.SetPipelineState(s_IBL_PSOCache["EquirectToCube"]);

		GfxContext.TransitionResource(g_EnvirMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		GfxContext.SetDynamicDescriptor(0, 0, g_IBLTexture.GetSRV());
		GfxContext.SetDynamicDescriptor(1, 0, g_EnvirMap.GetUAVArray()[0]);
		GfxContext.SetConstants(2, 0);
		GfxContext.SetConstants(3, 0);
		GfxContext.Dispatch(16, 16, 6);


		//// =======================  Generic MipMap
		GfxContext.SetDynamicDescriptor(0, 0, g_EnvirMap.GetSRV());
		GfxContext.SetPipelineState(s_IBL_PSOCache["GenerateMipMap"]);
	
		for (UINT level = 1, size = 256; level < 10; ++level, size /= 2)
		{
			GfxContext.SetDynamicDescriptor(1, 0, g_EnvirMap.GetUAVArray()[level]);

			const UINT numGroups = std::max(1u, size / 32);
		
			GfxContext.SetConstants(2, size);
			GfxContext.SetConstants(3, level - 1);
			GfxContext.Dispatch(numGroups, numGroups, 6);

			GfxContext.TransitionResource(g_EnvirMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		GfxContext.TransitionResource(g_EnvirMap, D3D12_RESOURCE_STATE_GENERIC_READ);
	}	
		
	// Compute pre-filtered specular environment map
	{	
		GfxContext.SetPipelineState(s_IBL_PSOCache["PrefilterSpecularMap"]);
		GfxContext.SetDynamicDescriptor(0, 0, g_EnvirMap.GetSRV());
		GfxContext.TransitionResource(g_RadianceMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);


		const UINT levels = g_RadianceMap.GetResource()->GetDesc().MipLevels;
		const float deltaRoughness = 1.0f / std::max(float(levels - 1), (float)1);
		for (UINT level = 0, size = 256; level < levels; ++level, size /= 2)
		{
			const UINT numGroups = std::max<UINT>(1, size / 32);
			const float spmapRoughness = level * deltaRoughness;
			GfxContext.SetDynamicDescriptor(1, 0, g_RadianceMap.GetUAVArray()[level]);
			GfxContext.SetConstants(2, spmapRoughness);
			GfxContext.Dispatch(numGroups, numGroups, 6);

			GfxContext.TransitionResource(g_RadianceMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		GfxContext.TransitionResource(g_RadianceMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	}	
		
		
		
	// create irradiance map compute pso
	{	
		
		GfxContext.SetPipelineState(s_IBL_PSOCache["PrecomputeIrradianceMap"]);


		GfxContext.SetDynamicDescriptor(0, 0, g_EnvirMap.GetSRV());
		GfxContext.SetDynamicDescriptor(1, 0, g_IrradianceMap.GetUAV());
		GfxContext.TransitionResource(g_IrradianceMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		GfxContext.Dispatch(2, 2, 6);
		GfxContext.TransitionResource(g_IrradianceMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
		
		
	{	
		
		GfxContext.SetPipelineState(s_IBL_PSOCache["PrecomputeBRDF"]);
		
		GfxContext.SetDynamicDescriptor(1, 0, g_LUT.GetUAV());
		GfxContext.TransitionResource(g_LUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		GfxContext.Dispatch(512/32, 512/32, 1);
		GfxContext.TransitionResource(g_LUT, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	}	
		
	{	

		GfxContext.SetPipelineState(s_IBL_PSOCache["emu"]);
		GfxContext.SetDynamicDescriptor(1, 0, g_Emu.GetUAV());
		GfxContext.TransitionResource(g_Emu, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		GfxContext.Dispatch(512 / 32, 512 / 32, 1);
		GfxContext.TransitionResource(g_Emu, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}	
		
	{
		GfxContext.SetPipelineState(s_IBL_PSOCache["eavg"]);

		GfxContext.SetDynamicDescriptor(1, 0, g_Eavg.GetUAV());
		GfxContext.TransitionResource(g_Eavg, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		GfxContext.Dispatch(512 / 32, 512 / 32, 1);
		GfxContext.TransitionResource(g_Eavg, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	GfxContext.Finish();
}



