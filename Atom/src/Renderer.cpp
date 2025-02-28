#include "pch.h"
#include "Renderer.h"
#include "stb_image/stb_image.h"
#include "ConstantBuffers.h"
#include "GraphicsCore.h"
// #include "ImGuizmo/ImGuizmo.h"

namespace VS
{
#include "../CompiledShaders/pbrVS.h"
#include "../CompiledShaders/SkyBoxVS.h"
#include "../CompiledShaders/TextureDebugVS.h"
#include "../CompiledShaders/ShadowVS.h"
#include "../CompiledShaders/DrawNormalsVS.h"
#include "../CompiledShaders/PostProcessVS.h"
#include "../CompiledShaders/BloomVS.h"
}

namespace PS
{
#include "../CompiledShaders/pbrPS.h"
#include "../CompiledShaders/SkyBoxPS.h"
#include "../CompiledShaders/TextureDebugPS.h"
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
}

namespace
{
    ComPtr<ID3D12RootSignature> computeRS;
    ComPtr<ID3D12PipelineState> cubePso;
    ComPtr<ID3D12PipelineState> irMapPso;
    ComPtr<ID3D12PipelineState> spMapPso;
    ComPtr<ID3D12PipelineState> lutPso;
}

using namespace Graphics;
using namespace Microsoft::WRL;
using namespace VS;
using namespace PS;
using namespace CS;
using namespace DirectX;
SsaoConstants ssaoCB; 

Application* CreateApplication(HINSTANCE hInstance)
{
    return new Renderer(hInstance);
}


Renderer::Renderer(HINSTANCE hInstance)
    : Application(hInstance)
{    
    ATOM_INFO("Render Initialize");

    Application::Initialize();


    ThrowIfFailed(m_CommandList->Reset(m_CommandAllocator.Get(), nullptr));

    m_Camera.SetPosition(0.0f, 2.0f, -5.0f);
    XMVECTOR lightPos = XMLoadFloat4(&XMFLOAT4{ 0.0f, 5.0f, 2.0f, 1.0f });
    XMStoreFloat4(&mLightPosW, lightPos);
    mShadowMap = std::make_unique<ShadowMap>(m_Device.Get(),
        2048, 2048);

    mSsao = std::make_unique<Ssao>(
        m_Device.Get(),
        m_CommandList.Get(),
        mClientWidth, mClientHeight);
    mSsao->Initialize();

   
    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildInputLayout();
    BuildShapeGeometry();
    
    CreateCubeMap();

    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(m_CommandList->Close()); 
    ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    {
        computeRS.Reset();
        cubePso.Reset();
        irMapPso.Reset();
        spMapPso.Reset();
        lutPso.Reset();
    }


    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    auto size = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 63, size);
    auto GpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 63, size);


    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(mhMainWnd);
    ImGui_ImplDX12_Init(m_Device.Get(), 2,
        mBackBufferFormat, m_SrvDescriptorHeap.Get(),
        handle,
        GpuHandle);


    Model skyBox, pbrModel, ground;

    skyBox.Load(std::string("D:/Atom/Atom/Assets/Models/cube.obj"), m_Device.Get(), m_CommandList.Get());
    pbrModel.Load(std::string("D:/Atom/Atom/Assets/Models/ball.obj"), m_Device.Get(), m_CommandList.Get());
    ground.Load(std::string("D:/Atom/Atom/Assets/Models/plane.obj"), m_Device.Get(), m_CommandList.Get());

    //pbrModel.modelMatrix = XMMatrixScaling(1, 1, 1);
    pbrModel.modelMatrix = XMMatrixTranslation(0, 1, 0);
    ground.modelMatrix = XMMatrixTranslation(0, 0, 0);

    pbrModel.normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, pbrModel.modelMatrix));
    ground.normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, ground.modelMatrix));
  
    m_SkyBox.model = std::move(skyBox);
    m_Scene.Models.push_back(std::move(pbrModel));
    m_Scene.Models.push_back(std::move(ground));

    m_MeshConstants.resize(m_Scene.Models.size());
    m_MeshConstantsBuffers.resize(m_Scene.Models.size());
    m_MaterialConstants.resize(m_Scene.Models.size());
    m_MaterialConstantsBuffers.resize(m_Scene.Models.size());
    InitConstantBuffer();
    mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
    mSceneBounds.Radius = 15;
}

Renderer::~Renderer()
{
    if(m_Device != nullptr)
        FlushCommandQueue();

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

bool Renderer::Initialize()
{
    
    return true;
}

void Renderer::CreateRtvAndDsvDescriptorHeaps()
{
    //2 swap chain buffer + 1 backend buffer + bloom buffer
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 2;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(m_Device->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(m_RtvHeap.GetAddressOf())));

    // Add 1 DSV for shadow map.
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 2;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    ThrowIfFailed(m_Device->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(m_DsvHeap.GetAddressOf())));
}
 
void Renderer::OnResize()
{
    Application::OnResize();

	m_Camera.SetLens(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

    // Resize color buffer
    if (isColorBufferInit)
    CreateColorBufferView();

   if(mSsao != nullptr)
   {
       mSsao->OnResize(mClientWidth, mClientHeight);
   
       mSsao->RebuildDescriptors(m_DepthStencilBuffer.Get());

       UINT descriptorRangeSize = 1;

       m_Device->CopyDescriptors(1,
           &GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::SsaoMapHeap),
           &descriptorRangeSize,
           1,
           &GetCpuHandle(mSsao->SsaoSrvHeap(), sceneColor0),
           &descriptorRangeSize,
           D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
       );

   }
}

void Renderer::Update(const GameTimer& gt)
{
    UpdateUI();
    m_Camera.Update(gt.DeltaTime());
    

    UpdateSsaoCB(gt);
}

void Renderer::Draw(const GameTimer& gt)
{
    ThrowIfFailed(m_CommandAllocator->Reset());

    ThrowIfFailed(m_CommandList->Reset(m_CommandAllocator.Get(), m_PSOs["opaque"].Get()));


    ID3D12DescriptorHeap* descriptorHeaps[] = { m_SrvDescriptorHeap.Get() };
    m_CommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());
      
	//
	// Shadow map pass.
	//

    DrawSceneToShadowMap();

	//
	// Normal/depth pass.
	//
	
	DrawNormalsAndDepth();
	
	//
	// Compute SSAO.
	// 

    mSsao->ComputeSsao(m_CommandList.Get(), ssaoCB, 3);

    m_CommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());
    m_CommandList->SetGraphicsRootDescriptorTable(kMaterialSRVs, m_SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	//
	// Main rendering pass.
	//
	
    // Indicate a state transition on the resource usage.
    m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
    static XMFLOAT4 color = XMFLOAT4(0, 0.2, 0.4, 1);
    // Clear the back buffer.
    m_CommandList->ClearRenderTargetView(CurrentBackBufferView(), &color.x, 0, nullptr);
    m_CommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    m_CommandList->OMSetRenderTargets(1, &m_ColorBufferRtvHandle, true, &DepthStencilView());

    m_CommandList->RSSetViewports(1, &m_ScreenViewport);
    m_CommandList->RSSetScissorRects(1, &m_ScissorRect);

    // Create Global Constant Buffer
    
   

    m_CommandList->SetGraphicsRootConstantBufferView(kCommonCBV, m_LightPassGlobalConstantsBuffer->GetGPUVirtualAddress());


    auto skyTexDescriptor = GetGpuHandle(m_SrvDescriptorHeap.Get(), int(DescriptorHeapLayout::ShpereMapHeap));
    m_CommandList->SetGraphicsRootDescriptorTable(kCommonSRVs, skyTexDescriptor);

    auto cubeMapDescriptor = GetGpuHandle(m_SrvDescriptorHeap.Get(), int(DescriptorHeapLayout::PrefilteredEnvirSrvHeap));
    m_CommandList->SetGraphicsRootDescriptorTable(kCubemapSrv, cubeMapDescriptor);

    auto irradianceMapDescriptor = GetGpuHandle(m_SrvDescriptorHeap.Get(), int(DescriptorHeapLayout::IrradianceMapSrvHeap));
    m_CommandList->SetGraphicsRootDescriptorTable(kIrradianceSrv, irradianceMapDescriptor);
       
    auto spMapDescriptor = GetGpuHandle(m_SrvDescriptorHeap.Get(), int(DescriptorHeapLayout::PrefilteredEnvirSrvHeap));
    m_CommandList->SetGraphicsRootDescriptorTable(kSpecularSrv, spMapDescriptor);
     
    auto lutMapDescriptor = GetGpuHandle(m_SrvDescriptorHeap.Get(), int(DescriptorHeapLayout::LUTsrv));
    m_CommandList->SetGraphicsRootDescriptorTable(kLUT, lutMapDescriptor);
    
    
    m_CommandList->SetPipelineState(m_PSOs["opaque"].Get());


    

    
    // shaderParameter cbuffer
    ComPtr<ID3D12Resource> shaderParamsCbuffer;
    {
        const UINT64 bufferSize = sizeof(ShaderParams);

        ThrowIfFailed(m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(shaderParamsCbuffer.GetAddressOf())
        ));
        BYTE* data = nullptr;
        shaderParamsCbuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
        //m_ShaderAttribs.roughness *= 0.5;
        memcpy(data, &m_ShaderAttribs, bufferSize);
        shaderParamsCbuffer->Unmap(0, nullptr);
    }
    m_CommandList->SetGraphicsRootConstantBufferView(kShaderParams, shaderParamsCbuffer->GetGPUVirtualAddress());

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
        m_CommandList->SetGraphicsRootConstantBufferView(kMeshConstants, m_MeshConstantsBuffers[i]->GetGPUVirtualAddress());
        m_CommandList->SetGraphicsRootConstantBufferView(kMaterialConstants, m_MaterialConstantsBuffers[i]->GetGPUVirtualAddress());

        m_Scene.Models[i].Draw(m_CommandList.Get());
    }   

    // material cbuffer
    ComPtr<ID3D12Resource> envMapBuffer;
    {
        const UINT64 bufferSize = sizeof(EnvMapRenderer::RenderAttribs);

        ThrowIfFailed(m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(envMapBuffer.GetAddressOf())
        ));
        BYTE* data = nullptr;
        envMapBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));

        memcpy(data, &m_EnvMapAttribs, bufferSize);
    }

    m_CommandList->SetGraphicsRootConstantBufferView(kMaterialConstants, envMapBuffer->GetGPUVirtualAddress());
   
    m_CommandList->SetPipelineState(m_PSOs["sky"].Get());
    m_SkyBox.model.Draw(m_CommandList.Get());

    // pick bright
    {
        m_CommandList->OMSetRenderTargets(1, &m_ColorBufferBrightRtvHandle, true, &DepthStencilView());

        auto bloomHandle = GetGpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ColorBufferSrv);
        m_CommandList->SetGraphicsRootDescriptorTable(kPostProcess, bloomHandle);
        m_CommandList->SetPipelineState(m_PSOs["bloom"].Get());
        m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        m_CommandList->DrawInstanced(4, 1, 0, 0);
    }


    m_CommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    // Post Process
    ComPtr<ID3D12Resource> ppBuffer;
    {
        const UINT64 bufferSize = sizeof(EnvMapRenderer::RenderAttribs);

        ThrowIfFailed(m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(ppBuffer.GetAddressOf())
        ));
        BYTE* data = nullptr;
        ppBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));

        memcpy(data, &m_ppAttribs, bufferSize);
    }

    m_CommandList->SetGraphicsRootConstantBufferView(kMaterialConstants, ppBuffer->GetGPUVirtualAddress());

    auto postProcessHandle = GetGpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ColorBufferSrv);
    m_CommandList->SetGraphicsRootDescriptorTable(kPostProcess, postProcessHandle);
    m_CommandList->SetPipelineState(m_PSOs["postprocess"].Get());
    m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    m_CommandList->DrawInstanced(4, 1, 0, 0); 


    //// Texture Debug
    D3D12_VIEWPORT LUTviewPort = { 0,0,256,256,0,1 };
    D3D12_RECT LUTrect = { 0,0,256,256 };
    
    m_CommandList->RSSetViewports(1, &LUTviewPort);
    m_CommandList->RSSetScissorRects(1, &LUTrect);
    
    m_CommandList->SetPipelineState(m_PSOs["debug"].Get());
    m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP); 
    m_CommandList->DrawInstanced(4, 1, 0, 0);
    

    // RenderingF
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_CommandList.Get());

    // Indicate a state transition on the resource usage.
	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(m_CommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(m_SwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}

void Renderer::UpdateSsaoCB(const GameTimer& gt)
{
    XMMATRIX P = m_Camera.GetProj();

    // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);

    XMStoreFloat4x4(&ssaoCB.Proj, P);
    XMMATRIX invProj = XMMatrixInverse(nullptr, P);   
    XMStoreFloat4x4(&ssaoCB.InvProj, invProj);

    XMStoreFloat4x4(&ssaoCB.ProjTex, P * T);

    mSsao->GetOffsetVectors(ssaoCB.OffsetVectors);

    auto blurWeights = mSsao->CalcGaussWeights(2.5f);
    ssaoCB.BlurWeights[0] = XMFLOAT4(&blurWeights[0]);
    ssaoCB.BlurWeights[1] = XMFLOAT4(&blurWeights[4]);
    ssaoCB.BlurWeights[2] = XMFLOAT4(&blurWeights[8]);

    ssaoCB.InvRenderTargetSize = XMFLOAT2(1.0f / mSsao->SsaoMapWidth(), 1.0f / mSsao->SsaoMapHeight());

   
    

}

void Renderer::UpdateUI()
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

        ImGui::Begin("DEBUG");                          // Create a window called "Hello, world!" and append into it.
 
        ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
        ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
        ImGui::Checkbox("Another Window", &show_another_window);

        ImGui::SliderFloat("Env mip map", &m_EnvMapAttribs.EnvMapMipLevel, 0.0f, 5.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::SliderFloat("exposure", &m_ppAttribs.exposure, 0.1f, 5.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
       
        ImGui::DragFloat4("LightPosition", &mLightPosW.x,0.3f,-90,90);
        ImGui::DragFloat4("CameraPosition", &m_Camera.GetPosition3f().x);

        ImGui::Checkbox("UseSsao", &m_ShaderAttribs.UseSSAO);
        ImGui::Checkbox("UseShadow", &m_ShaderAttribs.UseShadow);       
        ImGui::Checkbox("UseTexture", &m_ShaderAttribs.UseTexture);       
        if (m_ShaderAttribs.UseTexture == false)
        {
            ImGui::ColorPicker3("albedo", m_ShaderAttribs.albedo);
            ImGui::SliderFloat("metallic", &m_ShaderAttribs.metallic, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::SliderFloat("roughness", &m_ShaderAttribs.roughness, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f

           
        }
        if (m_ShaderAttribs.UseSSAO == true)
        {
            // Coordinates given in view space.
            ImGui::SliderFloat("OcclusionRadius",&ssaoCB.OcclusionRadius, 0.0f, 1.0f);
            ImGui::SliderFloat("OcclusionFadeStart",&ssaoCB.OcclusionFadeStart, 0.0f, 1.0f);
            ImGui::SliderFloat("OcclusionFadeEnd",&ssaoCB.OcclusionFadeEnd, 0.0f, 1.0f);
            ImGui::SliderFloat("SurfaceEpsilon",&ssaoCB.SurfaceEpsilon, 0.0f, 1.0f);
        }

        

        if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
          counter++;
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);



        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::End();
    }
}

void Renderer::InitConstantBuffer()
{
    ThrowIfFailed(m_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(GlobalConstantsBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(m_LightPassGlobalConstantsBuffer.GetAddressOf())
    ));

    ThrowIfFailed(m_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(GlobalConstantsBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(m_ShadowPassGlobalConstantsBuffer.GetAddressOf())
    ));

    for (int i = 0; i < m_Scene.Models.size(); i++)
    {
        ThrowIfFailed(m_Device->CreateCommittedResource(
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
        ThrowIfFailed(m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(MaterialConstantsBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(m_MaterialConstantsBuffers[i].GetAddressOf())
        ));
    }
}

void Renderer::LoadTextures()
{
	std::vector<std::string> texNames = 
	{
        "albedo",
        "wood_albedo",
        "normal",
        "wood_normal",
        "metallic",
        "wood_metallic",
        "roughness",
        "wood_roughness",
    
		"skyCubeMap"
	};
	
    std::vector<std::string> texFilenames =
    {
        "D:/Atom/Atom/Assets/Textures/silver/albedo.png",
        "D:/Atom/Atom/Assets/Textures/wood/albedo.png",

        "D:/Atom/Atom/Assets/Textures/silver/normal.png",
        "D:/Atom/Atom/Assets/Textures/wood/normal.png",

        "D:/Atom/Atom/Assets/Textures/silver/metallic.png",
        "D:/Atom/Atom/Assets/Textures/wood/metallic.png",

         "D:/Atom/Atom/Assets/Textures/silver/roughness.png",
         "D:/Atom/Atom/Assets/Textures/wood/roughness.png",
                   
        "D:/Atom/Atom/Assets/Textures/EnvirMap/environment.hdr"
    };
	for(int i = 0; i < (int)texNames.size() - 1; ++i)
	{
        auto texMap = std::make_unique<Texture>();
        texMap->Name = texNames[i];
        texMap->Filename = texFilenames[i];

        int width = 0, height = 0, channels = 0;
        
        UCHAR* imageData = stbi_load(texMap->Filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!imageData) {
            ATOM_ERROR("Failed to load material texture");
        }
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = 1;                     
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.Width = width;                     
        textureDesc.Height = height;                  
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;              
        textureDesc.SampleDesc.Count = 1;              
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; 


        ThrowIfFailed(m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,  
            nullptr,
            IID_PPV_ARGS(&texMap->Resource)
        ));
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texMap->Resource.Get(), 0, 1);

        ThrowIfFailed(m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&texMap->UploadHeap)
        ));
        
        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = imageData;       
        textureData.RowPitch = width * 4;    
        textureData.SlicePitch = textureData.RowPitch * height;

        UpdateSubresources(m_CommandList.Get(), texMap->Resource.Get(), texMap->UploadHeap.Get(), 0, 0, 1, &textureData);
		
		m_Textures[texMap->Name] = std::move(texMap);
	}		

    {
        stbi_set_flip_vertically_on_load(false);

        auto texMap = std::make_unique<Texture>();
        texMap->Name = texNames[texNames.size()-1];
        texMap->Filename = texFilenames[texNames.size() - 1];

        int width = 0, height = 0, channels = 0;
   
        float* imageData = stbi_loadf(texMap->Filename.c_str(), &width, &height, &channels, 4);

        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = 1;                     
        textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        textureDesc.Width = width;                    
        textureDesc.Height = height;                   
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;              
        textureDesc.SampleDesc.Count = 1;              
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; 

        ThrowIfFailed(m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,  
            nullptr,
            IID_PPV_ARGS(&texMap->Resource)
        ));
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texMap->Resource.Get(), 0, 1);
        //d3dUtil::CalcConstantBufferByteSize
        D3D12_RESOURCE_DESC uploadDesc = {};
        uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Alignment = 0;
        uploadDesc.Width = uploadBufferSize;  
        uploadDesc.Height = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels = 1;
        uploadDesc.Format = DXGI_FORMAT_UNKNOWN; 
        uploadDesc.SampleDesc.Count = 1;
        uploadDesc.SampleDesc.Quality = 0;
        uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ThrowIfFailed(m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&texMap->UploadHeap)
        ));


        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = imageData;      
        textureData.RowPitch = width * 4 * 4;    
        textureData.SlicePitch = textureData.RowPitch * height;

        ATOM_INFO(UpdateSubresources(m_CommandList.Get(), texMap->Resource.Get(), texMap->UploadHeap.Get(), 0, 0, 1, &textureData));

        m_Textures[texMap->Name] = std::move(texMap);
    }
}

void Renderer::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE materialSrv;
    materialSrv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 0, 0);

    CD3DX12_DESCRIPTOR_RANGE commonSrv;
    commonSrv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 10, 0);

     CD3DX12_DESCRIPTOR_RANGE cubemapRange;
     cubemapRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 13, 0);

     CD3DX12_DESCRIPTOR_RANGE irradianceRange;
     irradianceRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 14, 0);

     CD3DX12_DESCRIPTOR_RANGE specularRange;
     specularRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 15, 0);

     CD3DX12_DESCRIPTOR_RANGE lutRange;
     lutRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 16, 0);

     CD3DX12_DESCRIPTOR_RANGE postRange;
     postRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 17, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[kNumRootBindings];

	// Perfomance TIP: Order from most frequent to least frequent.
    slotRootParameter[kMeshConstants].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    slotRootParameter[kMaterialConstants].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[kMaterialSRVs].InitAsDescriptorTable(1, &materialSrv, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[kCommonSRVs].InitAsDescriptorTable(1, &commonSrv, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[kCommonCBV].InitAsConstantBufferView(1);
    slotRootParameter[kShaderParams].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);;
    slotRootParameter[kCubemapSrv].InitAsDescriptorTable(1, &cubemapRange, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[kIrradianceSrv].InitAsDescriptorTable(1, &irradianceRange, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[kSpecularSrv].InitAsDescriptorTable(1, &specularRange, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[kLUT].InitAsDescriptorTable(1, &lutRange, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[kPostProcess].InitAsDescriptorTable(1, &postRange, D3D12_SHADER_VISIBILITY_PIXEL);
  
    

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

    ThrowIfFailed(m_Device->CreateRootSignature(
		0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(m_RootSignature.GetAddressOf())));
}


void Renderer::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 64;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SrvDescriptorHeap)));

    
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	std::vector<ComPtr<ID3D12Resource>> tex2DList = 
	{
		m_Textures["albedo"]->Resource,m_Textures["wood_albedo"]->Resource,
		m_Textures["normal"]->Resource,m_Textures["wood_normal"]->Resource,
		m_Textures["metallic"]->Resource,m_Textures["wood_metallic"]->Resource,
		m_Textures["roughness"]->Resource,m_Textures["wood_roughness"]->Resource,
	};
	
	auto skyCubeMap = m_Textures["skyCubeMap"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	
    // Create Material Texture srv
	for(UINT i = 0; i < (UINT)tex2DList.size(); ++i)
	{
		srvDesc.Format = tex2DList[i]->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = tex2DList[i]->GetDesc().MipLevels;
		m_Device->CreateShaderResourceView(tex2DList[i].Get(), &srvDesc, hDescriptor);

		// next descriptor
		hDescriptor.Offset(1, CbvSrvUavDescriptorSize);
	}
	
    // Create SphereMap SRV
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = skyCubeMap->GetDesc().MipLevels;   
	srvDesc.Format = skyCubeMap->GetDesc().Format;
	m_Device->CreateShaderResourceView(skyCubeMap.Get(), &srvDesc, hDescriptor);


    auto nullSrv = GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::NullCubeCbvHeap);
    mNullSrv = GetGpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::NullCubeCbvHeap);

    m_Device->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
    nullSrv.Offset(1, CbvSrvUavDescriptorSize);

    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    m_Device->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

    nullSrv.Offset(1, CbvSrvUavDescriptorSize);
    m_Device->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

    mShadowMap->BuildDescriptors(
        GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ShadowMapHeap),
        GetGpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ShadowMapHeap),
        GetCpuHandle(m_DsvHeap.Get(), 1));

    // create 5 srv
    mSsao->BuildDescriptors(m_DepthStencilBuffer.Get());

    UINT descriptorRangeSize = 1;

    m_Device->CopyDescriptors(1,
        &GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::SsaoMapHeap),
        &descriptorRangeSize,
        1,
        &GetCpuHandle(mSsao->SsaoSrvHeap(), sceneColor0),
        &descriptorRangeSize,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );

    // Create environment Map
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Width = 256;
        desc.Height = 256;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Format = skyCubeMap->GetDesc().Format;
        desc.MipLevels = 9;
        desc.DepthOrArraySize = 6;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;

        ThrowIfFailed(m_Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_EnvirMap)));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = skyCubeMap->GetDesc().Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = -1;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.ResourceMinLODClamp = 0;

        auto envirSrv = GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::EnvirSrvHeap);

        m_Device->CreateShaderResourceView(m_EnvirMap.Get(), &srvDesc, envirSrv);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = skyCubeMap->GetDesc().Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        for (int i = 0; i < 9; i++)
        {
            uavDesc.Texture2DArray.ArraySize = 6;
            uavDesc.Texture2DArray.FirstArraySlice = 0;
            uavDesc.Texture2DArray.MipSlice = i;
            uavDesc.Texture2DArray.PlaneSlice = 0;
            auto envirUav = GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::EnvirUavHeap + i);

            m_Device->CreateUnorderedAccessView(m_EnvirMap.Get(), nullptr, &uavDesc, envirUav);

        }
    }


    // Create prefiltered environment Map 
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Width = 256;
        desc.Height = 256;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Format = skyCubeMap->GetDesc().Format;
        desc.MipLevels = 9;
        desc.DepthOrArraySize = 6;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;

        ThrowIfFailed(m_Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_PrefilteredEnvirMap)));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = skyCubeMap->GetDesc().Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = -1;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.ResourceMinLODClamp = 0;

        auto envirSrv = GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::PrefilteredEnvirSrvHeap);

        m_Device->CreateShaderResourceView(m_PrefilteredEnvirMap.Get(), &srvDesc, envirSrv);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = skyCubeMap->GetDesc().Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        for (int i = 0; i < 9; i++)
        {
            uavDesc.Texture2DArray.ArraySize = 6;
            uavDesc.Texture2DArray.FirstArraySlice = 0;
            uavDesc.Texture2DArray.MipSlice = i;
            uavDesc.Texture2DArray.PlaneSlice = 0;
            auto envirUav = GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::PrefilteredEnvirUavHeap + i);

            m_Device->CreateUnorderedAccessView(m_PrefilteredEnvirMap.Get(), nullptr, &uavDesc, envirUav);
        }
        
    }

    // Create irradiance Map 
    {
        D3D12_RESOURCE_DESC irMapDesc = {};
        irMapDesc.Width = 64;
        irMapDesc.Height = 64;
        irMapDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        irMapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        irMapDesc.MipLevels = 1;
        irMapDesc.DepthOrArraySize = 6;
        irMapDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        irMapDesc.SampleDesc.Count = 1;
        irMapDesc.SampleDesc.Quality = 0;

        ThrowIfFailed(m_Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &irMapDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_IrradianceMap)));

        D3D12_SHADER_RESOURCE_VIEW_DESC IrMapSrvDesc = {};
        IrMapSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        IrMapSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        IrMapSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        IrMapSrvDesc.TextureCube.MipLevels = -1;
        IrMapSrvDesc.TextureCube.MostDetailedMip = 0;
        IrMapSrvDesc.TextureCube.ResourceMinLODClamp = 0;

        auto irMapSrv = GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::IrradianceMapSrvHeap);

        m_Device->CreateShaderResourceView(m_IrradianceMap.Get(), &IrMapSrvDesc, irMapSrv);

        D3D12_UNORDERED_ACCESS_VIEW_DESC IrMapUavDesc = {};
        IrMapUavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        IrMapUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        IrMapUavDesc.Texture2DArray.ArraySize = 6;
        IrMapUavDesc.Texture2DArray.FirstArraySlice = 0;
        IrMapUavDesc.Texture2DArray.MipSlice = 0;
        IrMapUavDesc.Texture2DArray.PlaneSlice = 0;
        auto irMapUav = GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::IrradianceMapUavHeap);

        m_Device->CreateUnorderedAccessView(m_IrradianceMap.Get(), nullptr, &IrMapUavDesc, irMapUav);

    }

    // LUT
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Width = 512;
        desc.Height = 512;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Format = DXGI_FORMAT_R16G16_FLOAT;
        desc.MipLevels = 1;
        desc.DepthOrArraySize = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        
        ThrowIfFailed(m_Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_LUT)));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = -1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0;

        auto spbrdfSrv = GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::LUTsrv);

        m_Device->CreateShaderResourceView(m_LUT.Get(), &srvDesc, spbrdfSrv);


        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        uavDesc.Texture2D.PlaneSlice = 0;

        auto spbrdfUrv = GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::LUTuav);
        m_Device->CreateUnorderedAccessView(m_LUT.Get(), nullptr, &uavDesc, spbrdfUrv);
    }

    // Create color buffer srv with rtv
    CreateColorBufferView();
  
}

void Renderer::BuildInputLayout()
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

void Renderer::BuildShapeGeometry()
{  
    
}

void Renderer::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;
	
    ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    basePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    basePsoDesc.pRootSignature = m_RootSignature.Get();
    basePsoDesc.VS =
	{ 
		g_ppbrVS, 
		sizeof(g_ppbrVS)
	};
    basePsoDesc.PS =
	{ 
		g_ppbrPS,
		sizeof(g_ppbrPS)
	};
    basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    // basePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    basePsoDesc.BlendState.RenderTarget[0].BlendEnable = false;
    basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    basePsoDesc.SampleMask = UINT_MAX;
    basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    basePsoDesc.NumRenderTargets = 1;
    basePsoDesc.RTVFormats[0] = mBackBufferFormat;
    basePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    basePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    basePsoDesc.DSVFormat = mDepthStencilFormat;

    //
    // PSO for opaque objects.
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = basePsoDesc;
    opaquePsoDesc.DepthStencilState.DepthEnable = true;
    opaquePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    opaquePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&m_PSOs["opaque"])));
    
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
    ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&m_PSOs["shadow_opaque"])));
    // 
    // //
    // // PSO for debug layer.
    // //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = basePsoDesc;
    debugPsoDesc.pRootSignature = m_RootSignature.Get();
    debugPsoDesc.InputLayout = { nullptr, 0};
    debugPsoDesc.VS =
    {
        g_pTextureDebugVS,sizeof(g_pTextureDebugVS)
    };
    debugPsoDesc.PS =
    {
        g_pTextureDebugPS, sizeof(g_pTextureDebugPS)
    };
    ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&m_PSOs["debug"])));

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
    drawNormalsPsoDesc.RTVFormats[0] = Ssao::NormalMapFormat;
    drawNormalsPsoDesc.SampleDesc.Count = 1;
    drawNormalsPsoDesc.SampleDesc.Quality = 0;
    drawNormalsPsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&drawNormalsPsoDesc, IID_PPV_ARGS(&m_PSOs["drawNormals"])));
    
 
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
	ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&m_PSOs["sky"])));

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
    ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&ppPsoDesc, IID_PPV_ARGS(&m_PSOs["postprocess"])));


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
    ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&bloomPsoDesc, IID_PPV_ARGS(&m_PSOs["bloom"])));

}

void Renderer::DrawSceneToShadowMap()
{
    m_CommandList->RSSetViewports(1, &mShadowMap->Viewport());
    m_CommandList->RSSetScissorRects(1, &mShadowMap->ScissorRect());
    
    // Change to DEPTH_WRITE.
    m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
        D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));
    
    // Clear the back buffer and depth buffer.
    m_CommandList->ClearDepthStencilView(mShadowMap->Dsv(), 
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    
    // Specify the buffers we are going to render to.
    m_CommandList->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());

    
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

    m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());

    m_CommandList->SetPipelineState(m_PSOs["shadow_opaque"].Get());
    
    m_CommandList->SetGraphicsRootConstantBufferView(kCommonCBV, m_ShadowPassGlobalConstantsBuffer->GetGPUVirtualAddress());

    for (int i = 0; i < m_Scene.Models.size(); i++)
    {
        {
            XMStoreFloat4x4(&m_MeshConstants[i].ModelMatrix, m_Scene.Models[i].modelMatrix);

            BYTE* data = nullptr;
            m_MeshConstantsBuffers[i]->Map(0, nullptr, reinterpret_cast<void**>(&data));

            memcpy(data, &m_MeshConstants[i].ModelMatrix, MeshConstantsBufferSize);
            m_MeshConstantsBuffers[i]->Unmap(0, nullptr);
        }

        m_CommandList->SetGraphicsRootConstantBufferView(kMeshConstants, m_MeshConstantsBuffers[i]->GetGPUVirtualAddress());
        m_Scene.Models[i].Draw(m_CommandList.Get());

    }

   
    
    // Change back to GENERIC_READ so we can read the texture in a shader.
    m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
}

void Renderer::DrawNormalsAndDepth()
{
	m_CommandList->RSSetViewports(1, &m_ScreenViewport);
    m_CommandList->RSSetScissorRects(1, &m_ScissorRect);

	auto normalMap = mSsao->NormalMap();
	auto normalMapRtv = mSsao->NormalMapRtv();
	
    // Change to RENDER_TARGET.
    m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
        D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the screen normal map and depth buffer.
	float clearValue[] = {0.0f, 0.0f, 1.0f, 0.0f};
    m_CommandList->ClearRenderTargetView(normalMapRtv, clearValue, 0, nullptr);
    m_CommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
    m_CommandList->OMSetRenderTargets(1, &normalMapRtv, true, &DepthStencilView());

    // Bind the constant buffer for this pass.
    m_CommandList->SetPipelineState(m_PSOs["drawNormals"].Get());
    
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

    
    m_CommandList->SetGraphicsRootConstantBufferView(kCommonCBV, m_LightPassGlobalConstantsBuffer->GetGPUVirtualAddress());

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
        m_CommandList->SetGraphicsRootConstantBufferView(kMeshConstants, m_MeshConstantsBuffers[i]->GetGPUVirtualAddress());
        m_Scene.Models[i].Draw(m_CommandList.Get());
    }

    // Change back to GENERIC_READ so we can read the texture in a shader.
    m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
}
void Renderer::CreateColorBufferView()
{
    isColorBufferInit = true;


    D3D12_RESOURCE_DESC desc = {};
    desc.Width = mClientWidth;
    desc.Height = mClientHeight;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Format = mBackBufferFormat;
    desc.MipLevels = 1;
    desc.DepthOrArraySize = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET; 
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    ThrowIfFailed(m_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_ColorBuffer)));

    ThrowIfFailed(m_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_ColorBufferBright)));

    // Create rtv
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = mBackBufferFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;

    m_ColorBufferRtvHandle = GetCpuHandle(m_RtvHeap.Get(), 2);
    m_ColorBufferBrightRtvHandle = GetCpuHandle(m_RtvHeap.Get(), 3);

    m_Device->CreateRenderTargetView(
        m_ColorBuffer.Get(),
        &rtvDesc,
        m_ColorBufferRtvHandle
    );

    m_Device->CreateRenderTargetView(
        m_ColorBufferBright.Get(),
        &rtvDesc,
        m_ColorBufferBrightRtvHandle
    );

    // Create srv
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = mBackBufferFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = -1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0;

    auto srvHandle = GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ColorBufferSrv);

    m_Device->CreateShaderResourceView(
        m_ColorBuffer.Get(),
        &srvDesc,
        srvHandle);
    srvHandle.Offset(Graphics::CbvSrvUavDescriptorSize);
    m_Device->CreateShaderResourceView(
        m_ColorBufferBright.Get(),
        &srvDesc,
        srvHandle);
}


void Renderer::CreateCubeMap()
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
    
    ThrowIfFailed(m_Device->CreateRootSignature(
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
        ThrowIfFailed(m_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&cubePso)));

        ID3D12DescriptorHeap* descriptorHeaps[] = { m_SrvDescriptorHeap.Get() };
        m_CommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        m_CommandList->SetComputeRootSignature(computeRS.Get());
        m_CommandList->SetPipelineState(cubePso.Get());
        auto srvHandle = GetGpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ShpereMapHeap);
        m_CommandList->SetComputeRootDescriptorTable(0, srvHandle);
        
        m_CommandList->SetComputeRoot32BitConstant(2, 0, 0);
        m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMap.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

        for (UINT level = 0, size = 256; level < 10; ++level, size /= 2)
        {
            auto uavHandle = GetGpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::EnvirUavHeap + level);
            m_CommandList->SetComputeRootDescriptorTable(1, uavHandle);
            const UINT numGroups = std::max<UINT>(1, size / 32);
 
            m_CommandList->SetComputeRoot32BitConstant(3, level, 0);
            m_CommandList->Dispatch(numGroups, numGroups, 6);
        }
        
        

        m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMap.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));

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

        ThrowIfFailed(m_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&spMapPso)));

        // copy 0th mipMap level into destination environmentMap
        const D3D12_RESOURCE_BARRIER preCopyBarriers[] =
        {
            CD3DX12_RESOURCE_BARRIER::Transition(m_PrefilteredEnvirMap.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMap.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE)
        };
        const D3D12_RESOURCE_BARRIER postCopyBarriers[] = 
        {
            CD3DX12_RESOURCE_BARRIER::Transition(m_PrefilteredEnvirMap.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMap.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        };

        m_CommandList->ResourceBarrier(2, preCopyBarriers);
        for (UINT mipLevel = 0; mipLevel < 9; mipLevel++)
        {
            for (UINT arraySlice = 0; arraySlice < 6; ++arraySlice)
            {
                const UINT subresourceIndex = D3D12CalcSubresource(mipLevel, arraySlice, 0, m_EnvirMap.Get()->GetDesc().MipLevels, 6);
                m_CommandList->CopyTextureRegion(&CD3DX12_TEXTURE_COPY_LOCATION{ m_PrefilteredEnvirMap.Get(), subresourceIndex }, 0, 0, 0, &CD3DX12_TEXTURE_COPY_LOCATION{ m_EnvirMap.Get(), subresourceIndex }, nullptr);
            }
        }
        m_CommandList->ResourceBarrier(2, postCopyBarriers);


        m_CommandList->SetPipelineState(spMapPso.Get());
        auto srvHandle = GetGpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::PrefilteredEnvirSrvHeap);
        m_CommandList->SetComputeRootDescriptorTable(0, srvHandle);
        
        
        const UINT levels = m_PrefilteredEnvirMap.Get()->GetDesc().MipLevels;
        const float deltaRoughness = 1.0f / max(float(levels - 1), 1);
        for (UINT level = 0, size = 256; level < levels; ++level, size /= 2)
        {
            
            const UINT numGroups = std::max<UINT>(1, size / 32);
            const float spmapRoughness = level * deltaRoughness;
        
            auto uavHandle = GetGpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::PrefilteredEnvirUavHeap + level);
            m_CommandList->SetComputeRootDescriptorTable(1, uavHandle);

            m_CommandList->SetComputeRoot32BitConstants(2, 1, &spmapRoughness, 0);
            m_CommandList->Dispatch(numGroups, numGroups, 6);
        }
        m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_PrefilteredEnvirMap.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
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
        ThrowIfFailed(m_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&irMapPso)));


        m_CommandList->SetPipelineState(irMapPso.Get());
        auto srvHandle = GetGpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::EnvirSrvHeap);
        m_CommandList->SetComputeRootDescriptorTable(0, srvHandle);
        auto uavHandle = GetGpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::IrradianceMapUavHeap);
        m_CommandList->SetComputeRootDescriptorTable(1, uavHandle);
        m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_IrradianceMap.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        m_CommandList->Dispatch(2, 2, 6);
        m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_IrradianceMap.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
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
        ThrowIfFailed(m_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&lutPso)));


        m_CommandList->SetPipelineState(lutPso.Get());
        auto uavHandle = GetGpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::LUTuav);
        m_CommandList->SetComputeRootDescriptorTable(1, uavHandle);
        m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_LUT.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        m_CommandList->Dispatch(512/32, 512/32, 1);
        m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_LUT.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
    }
}


D3D12_CPU_DESCRIPTOR_HANDLE Renderer::CreateTextureUav(ID3D12Resource* res, UINT mipSlice)
{
    auto desc = res->GetDesc();

    assert(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = desc.Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    uavDesc.Texture2DArray.MipSlice = mipSlice;
    uavDesc.Texture2DArray.FirstArraySlice = 0;
    uavDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;

    auto descriptor = GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::IrradianceMapUavHeap + mipSlice);
    m_Device->CreateUnorderedAccessView(res, nullptr, &uavDesc, descriptor);
    return descriptor;
}



std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> Renderer::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
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


