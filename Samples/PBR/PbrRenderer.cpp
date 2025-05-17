#include "pch.h"
#include "Renderer.h"
#include "PbrRenderer.h"
#include "ConstantBuffers.h"
#include "GraphicsCommon.h"
#include "GraphicsCore.h"
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
#include "CommandContext.h"
namespace VS
{
#include "../CompiledShaders/PBRShadingVS.h"
#include "../CompiledShaders/SkyBoxVS.h"
#include "../CompiledShaders/ShadowVS.h"
#include "../CompiledShaders/DrawNormalsVS.h"
#include "../CompiledShaders/PostProcessVS.h"
#include "../CompiledShaders/BloomVS.h"
}

namespace PS
{
#include "../CompiledShaders/PBRShadingPS.h"
#include "../CompiledShaders/SkyBoxPS.h"
#include "../CompiledShaders/ShadowPS.h"
#include "../CompiledShaders/DrawNormalsPS.h"
#include "../CompiledShaders/PostProcessPS.h"
#include "../CompiledShaders/BloomPS.h"
}

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
	ComPtr<ID3D12RootSignature> computeRS;
	ComPtr<ID3D12PipelineState> cubePso;
	ComPtr<ID3D12PipelineState> irMapPso;
	ComPtr<ID3D12PipelineState> spMapPso;
	ComPtr<ID3D12PipelineState> lutPso;
	ComPtr<ID3D12PipelineState> mmPso;
	ComPtr<ID3D12PipelineState> emuPso;
	ComPtr<ID3D12PipelineState> eavgPso;
}

using namespace Graphics;
using namespace Renderer;
using namespace GameCore;
using namespace Microsoft::WRL;
using namespace VS;
using namespace PS;
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
	int a;

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
	
	//LoadTextures(gfxContext.GetCommandList());
	BuildRootSignature();
	BuildInputLayout();
	BuildShapeGeometry();
	g_IBLTexture = TextureManager::LoadHdrFromFile(L"D:/AtomEngine/Atom/Assets/Textures/EnvirMap/marry.hdr");
	BuildPSOs();

	CreateCubeMap(gfxContext.GetCommandList());

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
	ImGui_ImplDX12_Init(g_Device.Get(), 3,
		DXGI_FORMAT_R16G16B16A16_FLOAT, Renderer::s_TextureHeap.GetHeapPointer(),
		ImGuiHandle[0],
		ImGuiHandle[0]);
	

	Model skyBox, pbrModel, pbrModel2;

	skyBox.Load(std::wstring(L"D:/AtomEngine/Atom/Assets/Models/cube.obj"), g_Device.Get(), gfxContext.GetCommandList());
	pbrModel.Load(std::wstring(L"D:/AtomEngine/Atom/Assets/Models/MaterialBall.obj"), g_Device.Get(), gfxContext.GetCommandList());
	//pbrModel2.Load(std::string("D:/AtomEngine/Atom/Assets/Models/plane.obj"), g_Device.Get(), gfxContext.GetCommandList());

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

	gfxContext.Finish(true);

	{
		computeRS.Reset();
		cubePso.Reset();
		irMapPso.Reset();
		spMapPso.Reset();
		lutPso.Reset();
		mmPso.Reset();
	}
	
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
	CommandContext& gfxContext = CommandContext::Begin(L"Scene Render");

	gfxContext.GetCommandList()->RSSetViewports(1, &g_ViewPort);
	gfxContext.GetCommandList()->RSSetScissorRects(1, &g_Rect);


	gfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_DisplayPlane[g_CurrentBuffer].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	ID3D12DescriptorHeap* descriptorHeaps[] = { s_TextureHeap.GetHeapPointer() };
	gfxContext.GetCommandList()->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	gfxContext.GetCommandList()->SetGraphicsRootSignature(m_RootSignature.Get());
	
	// Z PrePass

	
	// Change to RENDER_TARGET.
	gfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SceneNormalBuffer.Resource.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));
	gfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SceneDepthBuffer.Resource.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	// Clear the screen normal map and depth buffer.
	static XMFLOAT4 color = XMFLOAT4(0, 0, 0, 1);
	gfxContext.GetCommandList()->ClearRenderTargetView(g_SceneNormalBuffer.RtvHandle, &color.x, 0, nullptr);
	gfxContext.GetCommandList()->ClearDepthStencilView(g_SceneDepthBuffer.DsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	gfxContext.GetCommandList()->OMSetRenderTargets(1, &g_SceneNormalBuffer.RtvHandle, true, &g_SceneDepthBuffer.DsvHandle);

	// Bind the constant buffer for this pass.
	gfxContext.GetCommandList()->SetPipelineState(m_PSOs["drawNormals"].Get());

	{
		BYTE* data = nullptr;
		m_LightPassGlobalConstantsBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));

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


	gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(kCommonCBV, m_LightPassGlobalConstantsBuffer->GetGPUVirtualAddress());

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
		gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(kMeshConstants, m_MeshConstantsBuffers[i]->GetGPUVirtualAddress());
		m_Scene.Models[i].Draw(gfxContext.GetCommandList());
	}

	// Change back to GENERIC_READ so we can read the texture in a shader.
	gfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SceneNormalBuffer.Resource.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON));

	// Render Shadow Map
	
	// Change to DEPTH_WRITE.
	gfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_ShadowBuffer.Resource.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	// Clear the back buffer and depth buffer.
	gfxContext.GetCommandList()->ClearDepthStencilView(g_ShadowBuffer.DsvHandle,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	gfxContext.GetCommandList()->OMSetRenderTargets(0, nullptr, false, &g_ShadowBuffer.DsvHandle);


	// upload to GPU
	{
		XMVECTOR lightPos = XMLoadFloat4(&mLightPosW);
		// Only the first "main" light casts a shadow.
		XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
		targetPos = XMVectorSetW(targetPos, 1);
		XMVECTOR lightUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
		XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);
		XMStoreFloat4x4(&m_ShadowPassGlobalConstants.ViewMatrix, lightView);

		// Transform bounding sphere to light space.
		XMFLOAT3 sphereCenterLS;
		XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

		// Ortho frustum in light space encloses scene.
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

	gfxContext.GetCommandList()->SetGraphicsRootSignature(m_RootSignature.Get());

	gfxContext.GetCommandList()->SetPipelineState(m_PSOs["shadow_opaque"].Get());

	gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(kCommonCBV, m_ShadowPassGlobalConstantsBuffer->GetGPUVirtualAddress());

	for (int i = 0; i < m_Scene.Models.size(); i++)
	{
		{
			XMStoreFloat4x4(&m_MeshConstants[i].ModelMatrix, m_Scene.Models[i].modelMatrix);

			BYTE* data = nullptr;
			m_MeshConstantsBuffers[i]->Map(0, nullptr, reinterpret_cast<void**>(&data));

			memcpy(data, &m_MeshConstants[i].ModelMatrix, MeshConstantsBufferSize);
			m_MeshConstantsBuffers[i]->Unmap(0, nullptr);
		}

		gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(kMeshConstants, m_MeshConstantsBuffers[i]->GetGPUVirtualAddress());
		m_Scene.Models[i].Draw(gfxContext.GetCommandList());

	}

	gfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_ShadowBuffer.Resource.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COMMON));

	
	
	// Render SSAO
	SSAO::Render(gfxContext, m_Camera);

	gfxContext.GetCommandList()->SetGraphicsRootDescriptorTable(Renderer::kCommonSRVs, Renderer::m_CommonTextures);



	gfxContext.GetCommandList()->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	gfxContext.GetCommandList()->SetGraphicsRootSignature(m_RootSignature.Get());

	gfxContext.GetCommandList()->SetPipelineState(m_PSOs["opaque"].Get());


	// light pass
	
	gfxContext.GetCommandList()->RSSetViewports(1, &g_ViewPort);
	gfxContext.GetCommandList()->RSSetScissorRects(1, &g_Rect);
	// Clear the back buffer.
	gfxContext.GetCommandList()->ClearRenderTargetView(g_BackBufferHandle[g_CurrentBuffer], &color.x, 0, nullptr);
	gfxContext.GetCommandList()->ClearDepthStencilView(g_SceneDepthBuffer.DsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	gfxContext.GetCommandList()->OMSetRenderTargets(1, &g_BackBufferHandle[g_CurrentBuffer], true, &g_SceneDepthBuffer.DsvHandle);

	gfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SceneColorBuffer.Resource.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Create Global Constant Buffer
	 
	gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(kCommonCBV, m_LightPassGlobalConstantsBuffer->GetGPUVirtualAddress());

	
	{        
		BYTE* data = nullptr;
		shaderParamsCbuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
		//m_ShaderAttribs.roughness *= 0.5;
		memcpy(data, &m_ShaderAttribs, shaderParamBufferSize);
		shaderParamsCbuffer->Unmap(0, nullptr);
		
	}
	gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(kShaderParams, shaderParamsCbuffer->GetGPUVirtualAddress());

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
		gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(kMeshConstants, m_MeshConstantsBuffers[i]->GetGPUVirtualAddress());
		gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(kMaterialConstants, m_MaterialConstantsBuffers[i]->GetGPUVirtualAddress());

		m_Scene.Models[i].Draw(gfxContext.GetCommandList());
	}   

	
	

	// // pick bright
	// {
	//     g_CommandList->OMSetRenderTargets(1, &m_ColorBufferBrightRtvHandle, true, &g_DsvHeap->GetCPUDescriptorHandleForHeapStart());
	// 
	//     auto bloomHandle = GetGpuHandle(g_SrvHeap.Get(), (int)DescriptorHeapLayout::ColorBufferSrv);
	//     g_CommandList->SetGraphicsRootDescriptorTable(kPostProcess, bloomHandle);
	//     g_CommandList->SetPipelineState(m_PSOs["bloom"].Get());
	//     g_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	// 
	//     g_CommandList->DrawInstanced(4, 1, 0, 0);
	// }

	{

		BYTE* data = nullptr;
		envMapBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));

		memcpy(data, &m_EnvMapAttribs, EnvMapAttribsBufferSize);
		envMapBuffer->Unmap(0, nullptr);
	}

	gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(kMaterialConstants, envMapBuffer->GetGPUVirtualAddress());

	gfxContext.GetCommandList()->SetPipelineState(m_PSOs["sky"].Get());
	m_SkyBox.model.Draw(gfxContext.GetCommandList());


	
	//gfxContext.GetCommandList()->OMSetRenderTargets(1, &g_BackBufferHandle[g_CurrentBuffer], true, &g_SceneDepthBuffer.DsvHandle);
	//gfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SceneColorBuffer.Resource.Get(),
	//	D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
	//
	//
	//
	//
	//{
	//	
	//	BYTE* data = nullptr;
	//	ppBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
	//
	//	memcpy(data, &m_ppAttribs, PostProcessBufferSize);
	//	ppBuffer->Unmap(0, nullptr);
	//}

	//gfxContext.GetCommandList()->SetGraphicsRootConstantBufferView(kMaterialConstants, ppBuffer->GetGPUVirtualAddress());
	//
	//gfxContext.GetCommandList()->SetGraphicsRootDescriptorTable(kPostProcess, g_SceneColorBuffer.RtvHandle);
	//gfxContext.GetCommandList()->SetPipelineState(m_PSOs["postprocess"].Get());
	//gfxContext.GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	//
	//gfxContext.GetCommandList()->DrawInstanced(4, 1, 0, 0);

	
	
	if (ImGui::Begin("Ssao Debug"))
	{
		ImVec2 winSize = ImGui::GetWindowSize();
		float smaller = (std::min)((winSize.x - 20), winSize.y - 20);
		ImGui::Image((ImTextureID)(g_PreComputeSrvHandle + 4*CbvSrvUavDescriptorSize).GetCpuPtr(), ImVec2(smaller, smaller));
	}
	ImGui::End();
	// RenderingF
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gfxContext.GetCommandList());

	gfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SceneColorBuffer.Resource.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));


	gfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_SceneDepthBuffer.Resource.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COMMON));

	// Indicate a state transition on the resource usage.
	gfxContext.GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_DisplayPlane[g_CurrentBuffer].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
	
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



void PbrRenderer::BuildRootSignature()
{
	using namespace Renderer;
	CD3DX12_DESCRIPTOR_RANGE materialSrv;
	materialSrv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE commonSrv;
	commonSrv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 10, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[kNumRootBindings] = {};

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[kMeshConstants].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	slotRootParameter[kMaterialConstants].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[kMaterialSRVs].InitAsDescriptorTable(1, &materialSrv, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[kCommonSRVs].InitAsDescriptorTable(1, &commonSrv, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[kCommonCBV].InitAsConstantBufferView(1);
	slotRootParameter[kShaderParams].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);;


	auto staticSamplers = GetStaticSamplers();
	
	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(kNumRootBindings, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if(errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(g_Device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(m_RootSignature.GetAddressOf())));
}



void PbrRenderer::BuildInputLayout()
{
	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

}

void PbrRenderer::BuildShapeGeometry()
{  
	
}

void PbrRenderer::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;
	
	ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	basePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	basePsoDesc.pRootSignature = m_RootSignature.Get();
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
	// basePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
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
	smapPsoDesc.pRootSignature = m_RootSignature.Get();
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

	// The camera is inside the sky sphere, so just turn off culling.
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	// Make sure the depth function is LESS_EQUAL and not just LESS.  
	// Otherwise, the normalized depth values at z = 1 (NDC) will 
	// fail the depth test if the depth buffer was cleared to 1.
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	skyPsoDesc.pRootSignature = m_RootSignature.Get();
	//skyPsoDesc.InputLayout.NumElements = (UINT)mInputLayout_Pos_UV.size();
	//skyPsoDesc.InputLayout.pInputElementDescs = mInputLayout_Pos_UV.data();
	skyPsoDesc.VS =
	{
		g_pSkyBoxVS,sizeof(g_pSkyBoxVS)
	};
	skyPsoDesc.PS =
	{
		g_pSkyBoxPS,sizeof(g_pSkyBoxPS)
	};
	ThrowIfFailed(g_Device->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&m_PSOs["sky"])));

	//
	// PSO for post process.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC ppPsoDesc = basePsoDesc;
	ppPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ppPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	ppPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ppPsoDesc.pRootSignature = m_RootSignature.Get();
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
	bloomPsoDesc.pRootSignature = m_RootSignature.Get();
	bloomPsoDesc.VS =
	{
		g_pBloomVS,sizeof(g_pBloomVS)
	};
	bloomPsoDesc.PS =
	{
		g_pBloomPS,sizeof(g_pBloomPS)
	};
	ThrowIfFailed(g_Device->CreateGraphicsPipelineState(&bloomPsoDesc, IID_PPV_ARGS(&m_PSOs["bloom"])));

}

#define SrvOffSetHandle(x) CD3DX12_GPU_DESCRIPTOR_HANDLE(g_PreComputeSrvHandle, x, CbvSrvUavDescriptorSize)
#define UavOffSetHandle(x) CD3DX12_GPU_DESCRIPTOR_HANDLE(g_PreComputeUavHandle, x, CbvSrvUavDescriptorSize)

enum SrvLayout
{
	ibl, env, radiance, irradiance ,lut, emu, eavg
};
enum UavLayout
{
	env0, env1, env2, env3, env4, env5, env6, env7, env8, env9,
	ra0, ra1, ra2, ra3, ra4, ra5, ra6, ra7, ra8,
	irra
};

#define OffsetDescriptor(x, y) CD3DX12_CPU_DESCRIPTOR_HANDLE(x, y, CbvSrvUavDescriptorSize)

void PbrRenderer::CreateCubeMap(ID3D12GraphicsCommandList* CmdList)
{
	
	// universal conpute root signature

	CD3DX12_DESCRIPTOR_RANGE range1 = {};
	range1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE range2 = {};
	range2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
	CD3DX12_ROOT_PARAMETER rootParameter[4];
	rootParameter[0].InitAsDescriptorTable(1, &range1);
	rootParameter[1].InitAsDescriptorTable(1, &range2);
	rootParameter[2].InitAsConstants(1, 0);
	rootParameter[3].InitAsConstants(1, 1);
	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, rootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	ThrowIfFailed(D3D12SerializeRootSignature(
		&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	
	ThrowIfFailed(g_Device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&computeRS)));


	// create cube map compute pso

	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.CS =
		{
			g_pEquirectToCubeCS,sizeof(g_pEquirectToCubeCS)
		};
		psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoDesc.pRootSignature = computeRS.Get();
		ThrowIfFailed(g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&cubePso)));

		psoDesc.CS =
		{
			g_pGenerateMipMapCS, sizeof(g_pGenerateMipMapCS)
		};
		ThrowIfFailed(g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&mmPso)));

		ID3D12DescriptorHeap* descriptorHeaps[] = { s_TextureHeap.GetHeapPointer()};
		CmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		g_PreComputeSrvHandle = s_TextureHeap.Alloc(7);
		g_PreComputeUavHandle = s_TextureHeap.Alloc(23);

		uint32_t UavDestCount = 23;
		uint32_t UavSourceCounts[] = 
		{ 
			1, 1, 1, 1, 1, 1, 1, 1,
			1, 1, 1, 1, 1, 1, 1, 1 ,
			1, 1, 1, 1, 1, 1, 1, 
		};

		uint32_t SrvDestCount = 7;
		uint32_t SrvSourceCounts[] =
		{
			1, 1, 1, 1, 1, 1, 1
		};

		D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[] =
		{
			g_IBLTexture.GetSRV(),
			g_EnvirMap.SrvHandle,
			g_RadianceMap.SrvHandle,
			g_IrradianceMap.SrvHandle,
			g_LUT.SrvHandle,
			g_Emu.SrvHandle,
			g_Eavg.SrvHandle,
		};

		D3D12_CPU_DESCRIPTOR_HANDLE UavSourceTextures[] =
		{
			g_EnvirMap.UavHandle[0],
			g_EnvirMap.UavHandle[1],
			g_EnvirMap.UavHandle[2],
			g_EnvirMap.UavHandle[3],
			g_EnvirMap.UavHandle[4],
			g_EnvirMap.UavHandle[5],
			g_EnvirMap.UavHandle[6],
			g_EnvirMap.UavHandle[7],
			g_EnvirMap.UavHandle[8],
			g_EnvirMap.UavHandle[9],

			g_RadianceMap.UavHandle[0],
			g_RadianceMap.UavHandle[1],
			g_RadianceMap.UavHandle[2],
			g_RadianceMap.UavHandle[3],
			g_RadianceMap.UavHandle[4],
			g_RadianceMap.UavHandle[5],
			g_RadianceMap.UavHandle[6],
			g_RadianceMap.UavHandle[7],
			g_RadianceMap.UavHandle[8],
			g_IrradianceMap.UavHandle[0],
			g_LUT.UavHandle[0],
			g_Emu.UavHandle[0],
			g_Eavg.UavHandle[0]

		};

		g_Device->CopyDescriptors(1, &g_PreComputeSrvHandle, &SrvDestCount, SrvDestCount, SourceTextures,
			SrvSourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		
		g_Device->CopyDescriptors(1, &g_PreComputeUavHandle, &UavDestCount, UavDestCount, UavSourceTextures,
			UavSourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


		CmdList->SetComputeRootSignature(computeRS.Get());
		CmdList->SetPipelineState(cubePso.Get());


		CmdList->SetComputeRootDescriptorTable(0, SrvOffSetHandle(ibl));
		
		
		CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_EnvirMap.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		
		CmdList->SetComputeRootDescriptorTable(1, UavOffSetHandle(env0));
		CmdList->SetComputeRoot32BitConstant(2, 0, 0);
		CmdList->SetComputeRoot32BitConstant(3, 0, 0);
		CmdList->Dispatch(16, 16, 6);
		//CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_EnvirMap.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));

		//// =======================
		CmdList->SetComputeRootDescriptorTable(0, SrvOffSetHandle(env));
		CmdList->SetPipelineState(mmPso.Get());
		for (UINT level = 1, size = 256; level < 10; ++level, size /= 2)
		{
			CmdList->SetComputeRootDescriptorTable(1, UavOffSetHandle(level));
			const UINT numGroups = std::max(1u, size / 32);
		
			CmdList->SetComputeRoot32BitConstant(2, size, 0);
			CmdList->SetComputeRoot32BitConstant(3, level-1, 0);
			
			CmdList->Dispatch(numGroups, numGroups, 6);
			D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(g_EnvirMap.Resource.Get());
			CmdList->ResourceBarrier(1, &uavBarrier);
		}
		
		
		
		CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_EnvirMap.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
		
	}	
		
	// Compute pre-filtered specular environment map
	{	
		
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.CS =
		{
		   g_pSpecularMapCS,
		   sizeof(g_pSpecularMapCS)
		};
		psoDesc.pRootSignature = computeRS.Get();
		
		ThrowIfFailed(g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&spMapPso)));
		
		// copy 0th mipMap level into destination environmentMap
		const D3D12_RESOURCE_BARRIER preCopyBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(g_RadianceMap.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(g_EnvirMap.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE)
		};
		const D3D12_RESOURCE_BARRIER postCopyBarriers[] = 
		{
			CD3DX12_RESOURCE_BARRIER::Transition(g_RadianceMap.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(g_EnvirMap.Resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON)
		};
		
		CmdList->ResourceBarrier(2, preCopyBarriers);
		for (UINT mipLevel = 1; mipLevel <= 9; mipLevel++)
		{
			for (UINT arraySlice = 0; arraySlice < 6; ++arraySlice)
			{
				const UINT srcSub = D3D12CalcSubresource(mipLevel, arraySlice, 0, g_EnvirMap.Resource.Get()->GetDesc().MipLevels, 6);
				const UINT destSub = D3D12CalcSubresource(mipLevel - 1, arraySlice, 0, g_EnvirMap.Resource.Get()->GetDesc().MipLevels - 1, 6);
				CmdList->CopyTextureRegion(&CD3DX12_TEXTURE_COPY_LOCATION{ g_RadianceMap.Resource.Get(), destSub }, 0, 0, 0, &CD3DX12_TEXTURE_COPY_LOCATION{ g_EnvirMap.Resource.Get(), srcSub }, nullptr);
			}
		}
		CmdList->ResourceBarrier(2, postCopyBarriers);
		
		
		CmdList->SetPipelineState(spMapPso.Get());
		CmdList->SetComputeRootDescriptorTable(0, SrvOffSetHandle(env));
		
		
		const UINT levels = g_RadianceMap.Resource.Get()->GetDesc().MipLevels;
		const float deltaRoughness = 1.0f / std::max(float(levels - 1), (float)1);
		for (UINT level = 0, size = 256; level < levels; ++level, size /= 2)
		{
			const UINT numGroups = std::max<UINT>(1, size / 32);
			const float spmapRoughness = level * deltaRoughness;
		
			CmdList->SetComputeRootDescriptorTable(1, UavOffSetHandle(level + 10));
		
			CmdList->SetComputeRoot32BitConstants(2, 1, &spmapRoughness, 0);
			CmdList->Dispatch(numGroups, numGroups, 6);
			D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(g_RadianceMap.Resource.Get());
			CmdList->ResourceBarrier(1, &uavBarrier);
		}
		CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_RadianceMap.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}	
		
		
		
	// create irradiance map compute pso
	{	
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.CS =
		{
			g_pIrradianceMapCS,
			sizeof(g_pIrradianceMapCS)
		};
		psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoDesc.pRootSignature = computeRS.Get();
		ThrowIfFailed(g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&irMapPso)));
		
		
		CmdList->SetPipelineState(irMapPso.Get());
		
		CmdList->SetComputeRootDescriptorTable(0, SrvOffSetHandle(env));
		
		CmdList->SetComputeRootDescriptorTable(1, UavOffSetHandle(irra));
		CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_IrradianceMap.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		CmdList->Dispatch(2, 2, 6);
		CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_IrradianceMap.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}	
		
		
	{	
		
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.CS =
		{
			g_pSpecularBRDFCS,
			sizeof(g_pSpecularBRDFCS)
		};
		psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoDesc.pRootSignature = computeRS.Get();
		ThrowIfFailed(g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&lutPso)));
		
		
		CmdList->SetPipelineState(lutPso.Get());
		
		CmdList->SetComputeRootDescriptorTable(1, UavOffSetHandle(20));
		CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_LUT.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		CmdList->Dispatch(512/32, 512/32, 1);
		CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_LUT.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}	
		
	{	
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.CS =
		{
			g_pEmu,
			sizeof(g_pEmu)
		};
		psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoDesc.pRootSignature = computeRS.Get();
		ThrowIfFailed(g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&emuPso)));
		
		
		CmdList->SetPipelineState(emuPso.Get());
		CmdList->SetComputeRootDescriptorTable(1, UavOffSetHandle(21));
		CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_Emu.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		CmdList->Dispatch(512 / 32, 512 / 32, 1);
		CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_Emu.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}	
		
	{	
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.CS =
		{
			g_pEavg,
			sizeof(g_pEavg)
		};
		psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoDesc.pRootSignature = computeRS.Get();
		ThrowIfFailed(g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&eavgPso)));
		
		
		CmdList->SetPipelineState(eavgPso.Get());
		CmdList->SetComputeRootDescriptorTable(1, UavOffSetHandle(22));
		CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_Eavg.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		CmdList->Dispatch(512 / 32, 512 / 32, 1);
		CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(g_Eavg.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}

}



std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> PbrRenderer::GetStaticSamplers()
{
	// GameCores usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC shadow(
		6, // shaderRegister
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
		0.0f,                               // mipLODBias
		16,                                 // maxAnisotropy
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);

	return { 
		pointWrap, pointClamp,
		linearWrap, linearClamp, 
		anisotropicWrap, anisotropicClamp,
		shadow 
	};
}


