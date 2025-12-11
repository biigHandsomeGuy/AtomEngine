#include "pch.h"
#include "Shadow.h"
#include "ConstantBuffers.h"
#include "GraphicsCore.h"

#include "stb_image/stb_image.h"
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx12.h"

namespace VS
{
#include "../CompiledShaders/LambertModelVS.h"
#include "../CompiledShaders/TextureDebugVS.h"
#include "../CompiledShaders/ShadowVS.h"
}

namespace PS
{
#include "../CompiledShaders/LambertModelPS.h"
#include "../CompiledShaders/TextureDebugPS.h"
#include "../CompiledShaders/ShadowPS.h"
}

using namespace DirectX::PackedVector;


using namespace Graphics;
using namespace Microsoft::WRL;
using namespace VS;
using namespace PS;
using namespace DirectX;

Application* CreateApplication(HINSTANCE hInstance)
{
    return new Renderer(hInstance);
}

std::vector<XMHALF4> ConvertToHalf(const float* floatData, int pixelCount) {
    std::vector<XMHALF4> halfData(pixelCount);
    for (int i = 0; i < pixelCount; i++) {
        halfData[i] = XMHALF4(
            floatData[i * 4 + 0],  // R
            floatData[i * 4 + 1],  // G
            floatData[i * 4 + 2],  // B
            floatData[i * 4 + 3]   // A
        );
    }
    return halfData;
}

Renderer::Renderer(HINSTANCE hInstance)
    : Application(hInstance)
{

    Application::Initialize();


    ASSERT_SUCCEEDED(m_CommandList->Reset(m_CommandAllocator.Get(), nullptr));

    m_Camera.SetPosition(0.0f, 5.0f, -10.0f);
    XMVECTOR lightPos = XMLoadFloat4(&XMFLOAT4{ 0.0f, 10.0f, 2.0f, 1.0f });
    XMStoreFloat4(&mLightPosW, lightPos);
    mShadowMap = std::make_unique<ShadowMap>(m_Device.Get(),
        2048, 2048);

    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildInputLayout();

    BuildPSOs();

    // Execute the initialization commands.
    ASSERT_SUCCEEDED(m_CommandList->Close());
    ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();


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


    Model ground;
    std::vector<Model> spheres(2);
    for (auto& sphere : spheres)
    {
        sphere.Load(std::string("D:/AtomEngine/Atom/Assets/Models/hpSphere.obj"), m_Device.Get(), m_CommandList.Get());
    }
    ground.Load(std::string("D:/AtomEngine/Atom/Assets/Models/plane.obj"), m_Device.Get(), m_CommandList.Get());


    spheres[0].modelMatrix = XMMatrixTranslation(0,1,0);
    spheres[1].modelMatrix = XMMatrixTranslation(0,4,0);
    
    

    ground.modelMatrix = XMMatrixScaling(10, 10, 10);
    for (auto& sphere : spheres)
    {
        sphere.normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, sphere.modelMatrix));

    }
    ground.normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, ground.modelMatrix));
    for (auto& sphere : spheres)
    {
        m_Scene.Models.push_back(std::move(sphere));

    }

    m_Scene.Models.push_back(std::move(ground));

    m_MeshConstants.resize(m_Scene.Models.size());
    m_MeshConstantsBuffers.resize(m_Scene.Models.size());

    InitConstantBuffer();
    mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
    mSceneBounds.Radius = 15;
}

Renderer::~Renderer()
{
    if (m_Device != nullptr)
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
    ASSERT_SUCCEEDED(m_Device->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(m_RtvHeap.GetAddressOf())));

    // Add 1 DSV for shadow map.
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 2;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    ASSERT_SUCCEEDED(m_Device->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(m_DsvHeap.GetAddressOf())));
}

void Renderer::OnResize()
{
    Application::OnResize();

    m_Camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void Renderer::Update(const GameTimer& gt)
{
    UpdateUI();
    m_Camera.Update(gt.DeltaTime());


}

void Renderer::Draw(const GameTimer& gt)
{
    ASSERT_SUCCEEDED(m_CommandAllocator->Reset());

    ASSERT_SUCCEEDED(m_CommandList->Reset(m_CommandAllocator.Get(), m_PSOs["opaque"].Get()));


    ID3D12DescriptorHeap* descriptorHeaps[] = { m_SrvDescriptorHeap.Get() };
    m_CommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());

    //
    // Shadow map pass.
    //

    DrawSceneToShadowMap();

    m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());

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


    m_CommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    m_CommandList->RSSetViewports(1, &m_ScreenViewport);
    m_CommandList->RSSetScissorRects(1, &m_ScissorRect);

    // Create Global Constant Buffer

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


    auto skyTexDescriptor = GetGpuHandle(m_SrvDescriptorHeap.Get(), int(DescriptorHeapLayout::ShadowMapHeap));
    m_CommandList->SetGraphicsRootDescriptorTable(kCommonSRVs, skyTexDescriptor);



    m_CommandList->SetPipelineState(m_PSOs["opaque"].Get());

    // shaderParameter cbuffer
    ComPtr<ID3D12Resource> shaderParamsCbuffer;
    {
        const UINT64 bufferSize = sizeof(ShaderParams);

        ASSERT_SUCCEEDED(m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(shaderParamsCbuffer.GetAddressOf())
        ));
        BYTE* data = nullptr;
        shaderParamsCbuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
        memcpy(data, &m_ShaderAttribs, bufferSize);
        shaderParamsCbuffer->Unmap(0, nullptr);
    }
    m_CommandList->SetGraphicsRootConstantBufferView(kShaderParams, shaderParamsCbuffer->GetGPUVirtualAddress());

    for (int i = 0; i < m_Scene.Models.size(); i++)
    {
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

        m_Scene.Models[i].Draw(m_CommandList.Get());
    }

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
    ASSERT_SUCCEEDED(m_CommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ASSERT_SUCCEEDED(m_SwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
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

        ImGui::DragFloat4("LightPosition", &mLightPosW.x, 0.3f, -90, 90);
        ImGui::DragFloat4("CameraPosition", &(m_Camera.GetPosition3f().x));

        ImGui::ColorPicker3("albedo", m_ShaderAttribs.albedo);
        ImGui::SliderFloat("light width", &m_ShaderAttribs.LightWidth, 0.01, 5);
        if(ImGui::BeginCombo("Shaodw Style", "choose"))
        {
            if (ImGui::Selectable("None"))
            {
                m_ShaderAttribs.UseBasicShadow = false;
                m_ShaderAttribs.UsePCFShadow = false;
                m_ShaderAttribs.UsePCSSShadow = false;
            }
            if (ImGui::Selectable("Basic"))
            {
                m_ShaderAttribs.UseBasicShadow = true;
                m_ShaderAttribs.UsePCFShadow = false;
                m_ShaderAttribs.UsePCSSShadow = false;
            }
            if (ImGui::Selectable("PCF"))
            {
                m_ShaderAttribs.UseBasicShadow = false;
                m_ShaderAttribs.UsePCFShadow = true;
                m_ShaderAttribs.UsePCSSShadow = false;
            }
            if (ImGui::Selectable("PCSS"))
            {
                m_ShaderAttribs.UseBasicShadow = false;
                m_ShaderAttribs.UsePCFShadow = false;
                m_ShaderAttribs.UsePCSSShadow = true;
            }

            ImGui::EndCombo();
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
    ASSERT_SUCCEEDED(m_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(GlobalConstantsBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(m_LightPassGlobalConstantsBuffer.GetAddressOf())
    ));

    ASSERT_SUCCEEDED(m_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(GlobalConstantsBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(m_ShadowPassGlobalConstantsBuffer.GetAddressOf())
    ));

    for (int i = 0; i < m_Scene.Models.size(); i++)
    {
        ASSERT_SUCCEEDED(m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(MeshConstantsBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(m_MeshConstantsBuffers[i].GetAddressOf())
        ));
    }

}


void Renderer::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE commonSrv;
    commonSrv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[kNumRootBindings];

    // Perfomance TIP: Order from most frequent to least frequent.
    slotRootParameter[kMeshConstants].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    slotRootParameter[kMaterialConstants].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
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

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ASSERT_SUCCEEDED(hr);

    ASSERT_SUCCEEDED(m_Device->CreateRootSignature(
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
    ASSERT_SUCCEEDED(m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SrvDescriptorHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    mShadowMap->BuildDescriptors(
        GetCpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ShadowMapHeap),
        GetGpuHandle(m_SrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ShadowMapHeap),
        GetCpuHandle(m_DsvHeap.Get(), 1));

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

void Renderer::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;

    ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    basePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    basePsoDesc.pRootSignature = m_RootSignature.Get();
    basePsoDesc.VS =
    {
        g_pLambertModelVS,
        sizeof(g_pLambertModelVS)
    };
    basePsoDesc.PS =
    {
        g_pLambertModelPS,
        sizeof(g_pLambertModelPS)
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
    ASSERT_SUCCEEDED(m_Device->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&m_PSOs["opaque"])));

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
    ASSERT_SUCCEEDED(m_Device->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&m_PSOs["shadow_opaque"])));
    // 
    // //
    // // PSO for debug layer.
    // //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = basePsoDesc;
    debugPsoDesc.pRootSignature = m_RootSignature.Get();
    debugPsoDesc.InputLayout = { nullptr, 0 };
    debugPsoDesc.VS =
    {
        g_pTextureDebugVS,sizeof(g_pTextureDebugVS)
    };
    debugPsoDesc.PS =
    {
        g_pTextureDebugPS, sizeof(g_pTextureDebugPS)
    };
    ASSERT_SUCCEEDED(m_Device->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&m_PSOs["debug"])));


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

        XMMATRIX T(
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, -0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f);

        XMMATRIX S = lightView * lightProj * T;
        XMStoreFloat4x4(&mShadowTransform, S);
        BYTE* data = nullptr;
        ASSERT_SUCCEEDED(m_ShadowPassGlobalConstantsBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data)));

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


