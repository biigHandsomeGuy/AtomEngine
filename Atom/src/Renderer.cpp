#include "pch.h"
#include "Renderer.h"
#include "stb_image/stb_image.h"
#include "ConstantBuffers.h"
#include "GraphicsCore.h"


namespace VS
{
#include "../CompiledShaders/pbrVS.h"
#include "../CompiledShaders/SkyBoxVS.h"
#include "../CompiledShaders/TextureDebugVS.h"
#include "../CompiledShaders/ShadowVS.h"
#include "../CompiledShaders/DrawNormalsVS.h"
#include "../CompiledShaders/PostProcessVS.h"
}

namespace PS
{
#include "../CompiledShaders/pbrPS.h"
#include "../CompiledShaders/SkyBoxPS.h"
#include "../CompiledShaders/TextureDebugPS.h"
#include "../CompiledShaders/ShadowPS.h"
#include "../CompiledShaders/DrawNormalsPS.h"
#include "../CompiledShaders/PostProcessPS.h"
}

namespace CS
{
#include "../CompiledShaders/SpecularBRDFCS.h"
#include "../CompiledShaders/IrradianceMapCS.h"
#include "../CompiledShaders/SpecularMapCS.h"
#include "../CompiledShaders/EquirectToCubeCS.h"
}

using namespace Graphics;
using namespace Microsoft::WRL;
using namespace VS;
using namespace PS;
using namespace CS;
SsaoConstants ssaoCB; 

Application* CreateApplication(HINSTANCE hInstance)
{
    return new Renderer(hInstance);
}


Renderer::Renderer(HINSTANCE hInstance)
    : Application(hInstance)
{
    
    // Estimate the scene bounding sphere manually since we know how the scene was constructed.
    // The grid is the "widest object" with a width of 20 and depth of 30.0f, and centered at
    // the world space origin.  In general, you need to loop over every world space vertex
    // position and compute the bounding sphere.
    mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
    mSceneBounds.Radius = 15;
}

Renderer::~Renderer()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

bool Renderer::Initialize()
{
    if(!Application::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCamera.SetPosition(0.0f, 8.0f, -15.0f);
    XMVECTOR lightPos = XMLoadFloat4(&XMFLOAT4{ 0.0f, 5.0f, 2.0f, 1.0f });
    XMStoreFloat4(&mLightPosW, lightPos);
    mShadowMap = std::make_unique<ShadowMap>(md3dDevice.Get(),
        2048, 2048);

    mSsao = std::make_unique<Ssao>(
        md3dDevice.Get(),
        mCommandList.Get(),
        mClientWidth, mClientHeight);
    mSsao->Initialize();
    m_pbrModelMatrix = XMMatrixScaling(0.1, 0.1, 0.1);
    m_pbrModelMatrix *= XMMatrixRotationX(30);


    m_GroundModelMatrix = XMMatrixScaling(30,1,30);
    m_GroundModelMatrix *= XMMatrixTranslation(0, -8, 0);

	LoadTextures();
    BuildRootSignature();
	BuildDescriptorHeaps();
    BuildInputLayout();
    BuildShapeGeometry();
    CreateCubeMap();
   
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();


    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    auto size = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 63, size);
    auto GpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 63, size);


    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(mhMainWnd);
    ImGui_ImplDX12_Init(md3dDevice.Get(), 2,
        mBackBufferFormat, mSrvDescriptorHeap.Get(),
        handle,
        GpuHandle);

    return true;
}

void Renderer::CreateRtvAndDsvDescriptorHeaps()
{
    //
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

    // Add +1 DSV for shadow map.
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 2;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));


}
 
void Renderer::OnResize()
{
    Application::OnResize();

	mCamera.SetLens(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

    // Resize color buffer
    if (isColorBufferInit)
    CreateColorBufferView();

   if(mSsao != nullptr)
   {
       mSsao->OnResize(mClientWidth, mClientHeight);
   
       // Resources changed, so need to rebuild descriptors.
       mSsao->RebuildDescriptors(mDepthStencilBuffer.Get());

       UINT descriptorRangeSize = 1;

       md3dDevice->CopyDescriptors(1,
           &GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::SsaoMapHeap),
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
    mCamera.Update(gt.DeltaTime());
    

    //
    // Animate the lights (and hence shadows).
    //

    UpdateSsaoCB(gt);
}

void Renderer::Draw(const GameTimer& gt)
{

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(mDirectCmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSOs["opaque"].Get()));


    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
   
    
	//
	// Shadow map pass.
	//

    // Bind all the materials used in this scene.  For structured buffers, we can bypass the heap and 
    // set as a root descriptor.
   
    // Bind null SRV for shadow map pass.
    // mCommandList->SetGraphicsRootDescriptorTable(kCommonSRVs, mNullSrv);	 

    // Bind all the textures used in this scene.  Observe
    // that we only have to specify the first descriptor in the table.  
    // The root signature knows how many descriptors are expected in the table.
    
    // 生成光源角度的 shadow map
    DrawSceneToShadowMap();

	//
	// Normal/depth pass.
	//
	
	DrawNormalsAndDepth();
	
	//
	// Compute SSAO.
	// 

    mSsao->ComputeSsao(mCommandList.Get(), ssaoCB, 1);

    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);


    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    mCommandList->SetGraphicsRootDescriptorTable(kMaterialSRVs, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	//
	// Main rendering pass.
	//
	
    // Rebind state whenever graphics root signature changes.

    // Bind all the materials used in this scene.  For structured buffers, we can bypass the heap and 
    // set as a root descriptor.
    
    // Indicate a state transition on the resource usage.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
    static XMFLOAT4 color = XMFLOAT4(0, 0.2, 0.4, 1);
    // Clear the back buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), &color.x, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // WE ALREADY WROTE THE DEPTH INFO TO THE DEPTH BUFFER IN DrawNormalsAndDepth,
    // SO DO NOT CLEAR DEPTH.

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &m_ColorBufferRtvHandle, true, &DepthStencilView());

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Bind all the textures used in this scene.  Observe
    // that we only have to specify the first descriptor in the table.  
    // The root signature knows how many descriptors are expected in the table.
    // mCommandList->SetGraphicsRootDescriptorTable(kCommonSRVs, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	
	
    // Create Global Constant Buffer
    ComPtr<ID3D12Resource> globalCbuffer;
    {        
        GlobalConstants globalBuffer = {};
        const UINT64 bufferSize = sizeof(GlobalConstants);

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(globalCbuffer.GetAddressOf())
        ));
        BYTE* data = nullptr;
        globalCbuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));

        XMMATRIX view = mCamera.GetView();
        XMMATRIX proj = mCamera.GetProj();
        XMMATRIX viewProj = XMMatrixMultiply(view, proj);

        XMStoreFloat4x4(&globalBuffer.ViewMatrix, view);
        XMStoreFloat4x4(&globalBuffer.ProjMatrix, proj);
        XMStoreFloat4x4(&globalBuffer.ViewProjMatrix, viewProj);
        globalBuffer.SunShadowMatrix = mShadowTransform;
        globalBuffer.CameraPos = mCamera.GetPosition3f();
        globalBuffer.SunPos = { mLightPosW.x,mLightPosW.y,mLightPosW.z};
        memcpy(data, &globalBuffer, bufferSize);
        globalCbuffer->Unmap(0, nullptr);
    }

    mCommandList->SetGraphicsRootConstantBufferView(kCommonCBV, globalCbuffer->GetGPUVirtualAddress());


    // Bind the sky cube map.  For our demos, we just use one "world" cube map representing the environment
    // from far away, so all objects will use the same cube map and we only need to set it once per-frame.  
    // If we wanted to use "local" cube maps, we would have to change them per-object, or dynamically
    // index into an array of cube maps.

    auto skyTexDescriptor = GetGpuHandle(mSrvDescriptorHeap.Get(), int(DescriptorHeapLayout::ShpereMapHeap));
    mCommandList->SetGraphicsRootDescriptorTable(kCommonSRVs, skyTexDescriptor);

    

    auto cubeMapDescriptor = GetGpuHandle(mSrvDescriptorHeap.Get(), int(DescriptorHeapLayout::EnvirSrvHeap));
    mCommandList->SetGraphicsRootDescriptorTable(kCubemapSrv, cubeMapDescriptor);

    auto irradianceMapDescriptor = GetGpuHandle(mSrvDescriptorHeap.Get(), int(DescriptorHeapLayout::IrradianceMapSrvHeap));
    mCommandList->SetGraphicsRootDescriptorTable(kIrradianceSrv, irradianceMapDescriptor);
       
    auto spMapDescriptor = GetGpuHandle(mSrvDescriptorHeap.Get(), int(DescriptorHeapLayout::EnvirSrvHeap));
    mCommandList->SetGraphicsRootDescriptorTable(kSpecularSrv, spMapDescriptor);
     
    auto lutMapDescriptor = GetGpuHandle(mSrvDescriptorHeap.Get(), int(DescriptorHeapLayout::LUTsrv));
    mCommandList->SetGraphicsRootDescriptorTable(kLUT, lutMapDescriptor);
    
    
   
    mCommandList->SetPipelineState(mPSOs["opaque"].Get());

    ComPtr<ID3D12Resource> meshCbuffer;
    {
        __declspec(align(256)) struct MeshConstants
        {
            DirectX::XMFLOAT4X4 World;
            DirectX::XMFLOAT4X4 ViewProjTex;
        } meshConstants;

        XMStoreFloat4x4(&meshConstants.World, m_pbrModelMatrix);
        XMMATRIX T(
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, -0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f);
        XMMATRIX viewProjTex = mCamera.GetView() * mCamera.GetProj() * T;
        XMStoreFloat4x4(&meshConstants.ViewProjTex, viewProjTex);
       
        const UINT64 bufferSize = sizeof(MeshConstants);

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(meshCbuffer.GetAddressOf())
        ));
        BYTE* data = nullptr;
        meshCbuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));

        memcpy(data, &meshConstants, bufferSize);
        meshCbuffer->Unmap(0, nullptr);
    }

    // material cbuffer
    ComPtr<ID3D12Resource> materialCbuffer;
    {
        MaterialConstants materialBuffer = {};
        const UINT64 bufferSize = sizeof(MaterialConstants);

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(materialCbuffer.GetAddressOf())
        ));
        BYTE* data = nullptr;
        materialCbuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));

        materialBuffer.gMatIndex = 0;
        memcpy(data, &materialBuffer, bufferSize);
        materialCbuffer->Unmap(0, nullptr);
    }
    
    // shaderParameter cbuffer
    ComPtr<ID3D12Resource> shaderParamsCbuffer;
    {
        const UINT64 bufferSize = sizeof(ShaderParams);

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
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
    mCommandList->SetGraphicsRootConstantBufferView(kMaterialConstants, materialCbuffer->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootConstantBufferView(kShaderParams, shaderParamsCbuffer->GetGPUVirtualAddress());

    {
        for (uint32_t meshIndex = 0; meshIndex < m_PbrModel.meshes.size(); meshIndex++)
        {
            mCommandList->SetGraphicsRootConstantBufferView(kMeshConstants, meshCbuffer->GetGPUVirtualAddress());
            mCommandList->IASetVertexBuffers(0, 1, &m_PbrModel.meshes[meshIndex].vbv);
            mCommandList->IASetIndexBuffer(&m_PbrModel.meshes[meshIndex].ibv);
            mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            mCommandList->DrawIndexedInstanced(m_PbrModel.meshes[meshIndex].Indices.size(), 1, 0, 0, 0);
        }

        
    }


    //XMStoreFloat4x4(&groundOC.World, m_GroundModelMatrix);
    //res->CopyData(1, groundOC);
    //
    //mCommandList->SetGraphicsRoot32BitConstant(10, 1, 0);
    //mCommandList->SetGraphicsRootConstantBufferView(0, res->Resource()->GetGPUVirtualAddress() + ocSize);
    //mCommandList->IASetVertexBuffers(0, 1, &m_Ground.vbv);
    //mCommandList->IASetIndexBuffer(&m_Ground.ibv);
    //mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    //mCommandList->DrawIndexedInstanced(m_Ground.numElements, 1, 0, 0, 0);

    // material cbuffer
    ComPtr<ID3D12Resource> envMapBuffer;
    {
        const UINT64 bufferSize = sizeof(EnvMapRenderer::RenderAttribs);

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
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

    mCommandList->SetGraphicsRootConstantBufferView(kMaterialConstants, envMapBuffer->GetGPUVirtualAddress());
   
    mCommandList->SetPipelineState(mPSOs["sky"].Get());
    mCommandList->IASetVertexBuffers(0, 1, &m_SkyBox.meshes[0].vbv);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetIndexBuffer(&m_SkyBox.meshes[0].ibv);
    mCommandList->DrawIndexedInstanced(m_SkyBox.meshes[0].Indices.size(), 1, 0, 0, 0);

    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());


    // Post Process

    ComPtr<ID3D12Resource> ppBuffer;
    {
        const UINT64 bufferSize = sizeof(EnvMapRenderer::RenderAttribs);

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
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

    mCommandList->SetGraphicsRootConstantBufferView(kMaterialConstants, ppBuffer->GetGPUVirtualAddress());

    auto postProcessHandle = GetGpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ColorBufferSrv);
    mCommandList->SetGraphicsRootDescriptorTable(kPostProcess, postProcessHandle);
    mCommandList->SetPipelineState(mPSOs["postprocess"].Get());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    mCommandList->DrawInstanced(4, 1, 0, 0); // 绘制 4 个顶点


    //// Texture Debug
    D3D12_VIEWPORT LUTviewPort = { 0,0,256,256,0,1 };
    D3D12_RECT LUTrect = { 0,0,256,256 };
    
    mCommandList->RSSetViewports(1, &LUTviewPort);
    mCommandList->RSSetScissorRects(1, &LUTrect);
    
    mCommandList->SetPipelineState(mPSOs["debug"].Get());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP); 
    mCommandList->DrawInstanced(4, 1, 0, 0);
  



    // RenderingF
    ImGui::Render();
    // //mCommandList->SetDescriptorHeaps(1, &mSrvDescriptorHeap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrentFence++;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().

    const UINT64 fence = mCurrentFence;

    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), fence));

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if (mFence->GetCompletedValue() < fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

void Renderer::UpdateSsaoCB(const GameTimer& gt)
{
    XMMATRIX P = mCamera.GetProj();

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

    // Coordinates given in view space.
    ssaoCB.OcclusionRadius = 0.5f;
    ssaoCB.OcclusionFadeStart = 0.2f;
    ssaoCB.OcclusionFadeEnd = 1.0f;
    ssaoCB.SurfaceEpsilon = 0.05f;
    

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
         //Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);   

        ImGui::SliderFloat("Env mip map", &m_EnvMapAttribs.EnvMapMipLevel, 0.0f, 5.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::SliderFloat("exposure", &m_ppAttribs.exposure, 0.1f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
       
        ImGui::Checkbox("UseSsao", &m_ShaderAttribs.UseSSAO);
        
        ImGui::Checkbox("UseShadow", &m_ShaderAttribs.UseShadow);       


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
        "D:/Atom/Atom/Assets/Textures/Cerberus_by_Andrew_Maximov/albedo.tga",
        "D:/Atom/Atom/Assets/Textures/wood/albedo.png",
               
        "D:/Atom/Atom/Assets/Textures/Cerberus_by_Andrew_Maximov/normal.tga",
        "D:/Atom/Atom/Assets/Textures/wood/normal.png",
           
        "D:/Atom/Atom/Assets/Textures/Cerberus_by_Andrew_Maximov/metallic.tga",
        "D:/Atom/Atom/Assets/Textures/wood/metallic.png",
              
        "D:/Atom/Atom/Assets/Textures/Cerberus_by_Andrew_Maximov/roughness.tga",
            
        "D:/Atom/Atom/Assets/Textures/wood/roughness.png",
             
        "D:/Atom/Atom/Assets/Textures/EnvirMap/marry.hdr"
    };
    //stbi_set_flip_vertically_on_load(true);
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
        textureDesc.MipLevels = 1;                     // mip 级别
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 纹理格式
        textureDesc.Width = width;                     // 纹理宽度
        textureDesc.Height = height;                   // 纹理高度
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;              // 单层纹理
        textureDesc.SampleDesc.Count = 1;              // 不使用多重采样
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2D 纹理


        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,   // 初始状态为 COPY_DEST
            nullptr,
            IID_PPV_ARGS(&texMap->Resource)
        ));
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texMap->Resource.Get(), 0, 1);

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&texMap->UploadHeap)
        ));
        

        // 复制数据到上传堆
        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = imageData;       // 图像数据
        textureData.RowPitch = width * 4;    // 每行字节数
        textureData.SlicePitch = textureData.RowPitch * height;

        // 将数据从上传堆复制到默认堆
        UpdateSubresources(mCommandList.Get(), texMap->Resource.Get(), texMap->UploadHeap.Get(), 0, 0, 1, &textureData);

		
		mTextures[texMap->Name] = std::move(texMap);
	}		

    {
        stbi_set_flip_vertically_on_load(false);

        auto texMap = std::make_unique<Texture>();
        texMap->Name = texNames[texNames.size()-1];
        texMap->Filename = texFilenames[texNames.size() - 1];

        int width = 0, height = 0, channels = 0;

        float* imageData = stbi_loadf(texMap->Filename.c_str(), &width, &height, &channels, 4);
        if (!imageData) {
            ATOM_ERROR("Failed to load skybox texture");
        }

        //DXGI_FORMAT_R16G16B16A16_FLOAT
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = 1;                     // mip 级别
        textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; // 纹理格式
        textureDesc.Width = width;                     // 纹理宽度
        textureDesc.Height = height;                   // 纹理高度
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;              // 单层纹理
        textureDesc.SampleDesc.Count = 1;              // 不使用多重采样
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2D 纹理

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,   // 初始状态为 COPY_DEST
            nullptr,
            IID_PPV_ARGS(&texMap->Resource)
        ));
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texMap->Resource.Get(), 0, 1);
        //d3dUtil::CalcConstantBufferByteSize
        D3D12_RESOURCE_DESC uploadDesc = {};
        uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Alignment = 0;
        uploadDesc.Width = uploadBufferSize;  // 数据总字节数（RGBA，每个 float 占 4 字节）
        uploadDesc.Height = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels = 1;
        uploadDesc.Format = DXGI_FORMAT_UNKNOWN; // 不需要格式
        uploadDesc.SampleDesc.Count = 1;
        uploadDesc.SampleDesc.Quality = 0;
        uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&texMap->UploadHeap)
        ));

        // 复制数据到上传堆
        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = imageData;       // 图像数据
        textureData.RowPitch = width * 16;    // 每行字节数
        textureData.SlicePitch = textureData.RowPitch * height;

        // 将数据从上传堆复制到默认堆
        ATOM_INFO(UpdateSubresources(mCommandList.Get(), texMap->Resource.Get(), texMap->UploadHeap.Get(), 0, 0, 1, &textureData));

        mTextures[texMap->Name] = std::move(texMap);
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
     postRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 17, 0);

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

    ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
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
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	std::vector<ComPtr<ID3D12Resource>> tex2DList = 
	{
		mTextures["albedo"]->Resource,mTextures["wood_albedo"]->Resource,
		mTextures["normal"]->Resource,mTextures["wood_normal"]->Resource,
		mTextures["metallic"]->Resource,mTextures["wood_metallic"]->Resource,
		mTextures["roughness"]->Resource,mTextures["wood_roughness"]->Resource,
	};
	
	auto skyCubeMap = mTextures["skyCubeMap"]->Resource;

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
		md3dDevice->CreateShaderResourceView(tex2DList[i].Get(), &srvDesc, hDescriptor);

		// next descriptor
		hDescriptor.Offset(1, CbvSrvUavDescriptorSize);
	}
	
    // Create SphereMap SRV
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = skyCubeMap->GetDesc().MipLevels;   
	srvDesc.Format = skyCubeMap->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(skyCubeMap.Get(), &srvDesc, hDescriptor);


    auto nullSrv = GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::NullCubeCbvHeap);
    mNullSrv = GetGpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::NullCubeCbvHeap);

    md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
    nullSrv.Offset(1, CbvSrvUavDescriptorSize);

    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

    nullSrv.Offset(1, CbvSrvUavDescriptorSize);
    md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

    mShadowMap->BuildDescriptors(
        GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ShadowMapHeap),
        GetGpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ShadowMapHeap),
        GetCpuHandle(mDsvHeap.Get(), 1));

    // create 5 srv
    mSsao->BuildDescriptors(mDepthStencilBuffer.Get());

    UINT descriptorRangeSize = 1;

    md3dDevice->CopyDescriptors(1,
        &GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::SsaoMapHeap),
        &descriptorRangeSize,
        1,
        &GetCpuHandle(mSsao->SsaoSrvHeap(), sceneColor0),
        &descriptorRangeSize,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );

    // Create environment Map unfilter
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Width = 1024;
        desc.Height = 1024;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Format = skyCubeMap->GetDesc().Format;
        desc.MipLevels = 6;
        desc.DepthOrArraySize = 6;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;

        ThrowIfFailed(md3dDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_EnvirMapUnfiltered)));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = skyCubeMap->GetDesc().Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = -1;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.ResourceMinLODClamp = 0;

        auto envirSrv = GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::EnvirUnfilterSrvHeap);

        md3dDevice->CreateShaderResourceView(m_EnvirMapUnfiltered.Get(), &srvDesc, envirSrv);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = skyCubeMap->GetDesc().Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        for (int i = 0; i < 6; i++)
        {
            uavDesc.Texture2DArray.ArraySize = 6;
            uavDesc.Texture2DArray.FirstArraySlice = 0;
            uavDesc.Texture2DArray.MipSlice = i;
            uavDesc.Texture2DArray.PlaneSlice = 0;
            auto envirUav = GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::EnvirUnfilterUavHeap + i);

            md3dDevice->CreateUnorderedAccessView(m_EnvirMapUnfiltered.Get(), nullptr, &uavDesc, envirUav);

        }
    }


    // Create environment Map 
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Width = 1024;
        desc.Height = 1024;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Format = skyCubeMap->GetDesc().Format;
        desc.MipLevels = 6;
        desc.DepthOrArraySize = 6;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;

        ThrowIfFailed(md3dDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
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

        auto envirSrv = GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::EnvirSrvHeap);

        md3dDevice->CreateShaderResourceView(m_EnvirMap.Get(), &srvDesc, envirSrv);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = skyCubeMap->GetDesc().Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.ArraySize = 6;
        uavDesc.Texture2DArray.FirstArraySlice = 0;
        uavDesc.Texture2DArray.MipSlice = 0;
        uavDesc.Texture2DArray.PlaneSlice = 0;
        auto envirUav = GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::EnvirUavHeap);

        md3dDevice->CreateUnorderedAccessView(m_EnvirMap.Get(), nullptr, &uavDesc, envirUav);

    }

    // Create irradiance Map 
    {
        D3D12_RESOURCE_DESC irMapDesc = {};
        irMapDesc.Width = 32;
        irMapDesc.Height = 32;
        irMapDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        irMapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        irMapDesc.MipLevels = 1;
        irMapDesc.DepthOrArraySize = 6;
        irMapDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        irMapDesc.SampleDesc.Count = 1;
        irMapDesc.SampleDesc.Quality = 0;

        ThrowIfFailed(md3dDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
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

        auto irMapSrv = GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::IrradianceMapSrvHeap);

        md3dDevice->CreateShaderResourceView(m_IrradianceMap.Get(), &IrMapSrvDesc, irMapSrv);

        D3D12_UNORDERED_ACCESS_VIEW_DESC IrMapUavDesc = {};
        IrMapUavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        IrMapUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        IrMapUavDesc.Texture2DArray.ArraySize = 6;
        IrMapUavDesc.Texture2DArray.FirstArraySlice = 0;
        IrMapUavDesc.Texture2DArray.MipSlice = 0;
        IrMapUavDesc.Texture2DArray.PlaneSlice = 0;
        auto irMapUav = GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::IrradianceMapUavHeap);

        md3dDevice->CreateUnorderedAccessView(m_IrradianceMap.Get(), nullptr, &IrMapUavDesc, irMapUav);

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
        
        ThrowIfFailed(md3dDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
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

        auto spbrdfSrv = GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::LUTsrv);

        md3dDevice->CreateShaderResourceView(m_LUT.Get(), &srvDesc, spbrdfSrv);


        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        uavDesc.Texture2D.PlaneSlice = 0;

        auto spbrdfUrv = GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::LUTuav);
        md3dDevice->CreateUnorderedAccessView(m_LUT.Get(), nullptr, &uavDesc, spbrdfUrv);
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
    m_SkyBox.Load(std::string("D:/Atom/Atom/Assets/Models/cube.fbx"),md3dDevice.Get(),mCommandList.Get());
    m_PbrModel.Load(std::string("D:/Atom/Atom/Assets/Models/Cerberus_LP.fbx"),md3dDevice.Get(),mCommandList.Get());
    m_Ground.Load(std::string("D:/Atom/Atom/Assets/Models/cube.fbx"),md3dDevice.Get(),mCommandList.Get());

}

void Renderer::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;
	
    ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    basePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    basePsoDesc.pRootSignature = mRootSignature.Get();
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
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));
    
    //
    // PSO for shadow map pass.
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = basePsoDesc;
    smapPsoDesc.RasterizerState.DepthBias = 100000;
    smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
    smapPsoDesc.pRootSignature = mRootSignature.Get();
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
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));
    // 
    // //
    // // PSO for debug layer.
    // //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = basePsoDesc;
    debugPsoDesc.pRootSignature = mRootSignature.Get();
    debugPsoDesc.InputLayout = { nullptr, 0};
    debugPsoDesc.VS =
    {
        g_pTextureDebugVS,sizeof(g_pTextureDebugVS)
    };
    debugPsoDesc.PS =
    {
        g_pTextureDebugPS, sizeof(g_pTextureDebugPS)
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"])));

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
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&drawNormalsPsoDesc, IID_PPV_ARGS(&mPSOs["drawNormals"])));
    
 
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
	skyPsoDesc.pRootSignature = mRootSignature.Get();
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
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));

    //
    // PSO for post process.
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC ppPsoDesc = basePsoDesc;

    // The camera is inside the sky sphere, so just turn off culling.
    ppPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    // Make sure the depth function is LESS_EQUAL and not just LESS.  
    // Otherwise, the normalized depth values at z = 1 (NDC) will 
    // fail the depth test if the depth buffer was cleared to 1.
    ppPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ppPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ppPsoDesc.pRootSignature = mRootSignature.Get();
    //skyPsoDesc.InputLayout.NumElements = (UINT)mInputLayout_Pos_UV.size();
    //skyPsoDesc.InputLayout.pInputElementDescs = mInputLayout_Pos_UV.data();
    ppPsoDesc.VS =
    {
        g_pPostProcessVS,sizeof(g_pPostProcessVS)
    };
    ppPsoDesc.PS =
    {
        g_pPostProcessPS,sizeof(g_pPostProcessPS)
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ppPsoDesc, IID_PPV_ARGS(&mPSOs["postprocess"])));



}

ComPtr<ID3D12Resource> meshBuffer;
void Renderer::DrawSceneToShadowMap()
{
    mCommandList->RSSetViewports(1, &mShadowMap->Viewport());
    mCommandList->RSSetScissorRects(1, &mShadowMap->ScissorRect());
    
    // Change to DEPTH_WRITE.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
        D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));
    
    // Clear the back buffer and depth buffer.
    mCommandList->ClearDepthStencilView(mShadowMap->Dsv(), 
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    
    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());

        

    {
        __declspec(align(256)) struct
        {
            XMFLOAT4X4 MVP;
        } vsConstants;

        const UINT64 bufferSize = sizeof(vsConstants);

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(meshBuffer.GetAddressOf())
        ));
        
        XMVECTOR lightPos = XMLoadFloat4(&mLightPosW);
        // Only the first "main" light casts a shadow.
        XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
        targetPos = XMVectorSetW(targetPos, 1);
        XMVECTOR lightUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);
       
       
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
        
        XMMATRIX viewProj = XMMatrixMultiply(lightView, lightProj);
        XMMATRIX mvp = XMMatrixMultiply(m_pbrModelMatrix, viewProj);
        XMStoreFloat4x4(&vsConstants.MVP, mvp);
        
		     // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
        XMMATRIX T(
         0.5f, 0.0f, 0.0f, 0.0f,
         0.0f, -0.5f, 0.0f, 0.0f,
         0.0f, 0.0f, 1.0f, 0.0f,
         0.5f, 0.5f, 0.0f, 1.0f);

        XMMATRIX S = lightView * lightProj * T;


        XMStoreFloat4x4(&mShadowTransform, S);

        BYTE* data = nullptr;
        ThrowIfFailed(meshBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data)));


        memcpy(data, &vsConstants, bufferSize);
        meshBuffer->Unmap(0, nullptr);
    }

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    mCommandList->SetPipelineState(mPSOs["shadow_opaque"].Get());
    
    mCommandList->SetGraphicsRootConstantBufferView(kMeshConstants, meshBuffer->GetGPUVirtualAddress());


    for (uint32_t meshIndex = 0; meshIndex < m_PbrModel.meshes.size(); meshIndex++)
    {
        mCommandList->IASetVertexBuffers(0, 1, &m_PbrModel.meshes[meshIndex].vbv);
        mCommandList->IASetIndexBuffer(&m_PbrModel.meshes[meshIndex].ibv);
        mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        mCommandList->DrawIndexedInstanced(m_PbrModel.meshes[meshIndex].Indices.size(), 1, 0, 0, 0);
    }

    //// Draw Ground
    //ObjectConstants groundOC;
    //
    //UINT ocSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    //XMStoreFloat4x4(&groundOC.World, m_GroundModelMatrix);
    //res->CopyData(1, groundOC);
    //mCommandList->SetGraphicsRoot32BitConstant(10, 1, 0);
    //
    //mCommandList->SetGraphicsRootConstantBufferView(0, res->Resource()->GetGPUVirtualAddress() + ocSize);
    //mCommandList->IASetVertexBuffers(0, 1, &m_Ground.vbv);
    //mCommandList->IASetIndexBuffer(&m_Ground.ibv);
    //mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    //mCommandList->DrawIndexedInstanced(m_Ground.numElements, 1, 0, 0, 0);
    
    
    
    // Change back to GENERIC_READ so we can read the texture in a shader.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
}
ComPtr<ID3D12Resource> globalCbuffer;
void Renderer::DrawNormalsAndDepth()
{
	mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

	auto normalMap = mSsao->NormalMap();
	auto normalMapRtv = mSsao->NormalMapRtv();
	
    // Change to RENDER_TARGET.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
        D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the screen normal map and depth buffer.
	float clearValue[] = {0.0f, 0.0f, 1.0f, 0.0f};
    mCommandList->ClearRenderTargetView(normalMapRtv, clearValue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &normalMapRtv, true, &DepthStencilView());

    // Bind the constant buffer for this pass.
    mCommandList->SetPipelineState(mPSOs["drawNormals"].Get());
    
    {
        __declspec(align(256)) struct
        {
            XMFLOAT4X4 model;
        } vsConstants;

        const UINT64 bufferSize = sizeof(vsConstants);

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(meshBuffer.GetAddressOf())
        ));

        XMStoreFloat4x4(&vsConstants.model, m_pbrModelMatrix);

        BYTE* data = nullptr;
        ThrowIfFailed(meshBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data)));


        memcpy(data, &vsConstants, bufferSize);
        meshBuffer->Unmap(0, nullptr);
    }
    // Create Global Constant Buffer
    
    {
        GlobalConstants globalBuffer = {};
        const UINT64 bufferSize = sizeof(GlobalConstants);

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(globalCbuffer.GetAddressOf())
        ));
        BYTE* data = nullptr;
        globalCbuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));

        XMMATRIX view = mCamera.GetView();
        XMMATRIX proj = mCamera.GetProj();
        XMMATRIX viewProj = XMMatrixMultiply(view, proj);

        XMStoreFloat4x4(&globalBuffer.ViewMatrix, view);
        XMStoreFloat4x4(&globalBuffer.ProjMatrix, proj);
        XMStoreFloat4x4(&globalBuffer.ViewProjMatrix, viewProj);
        globalBuffer.SunShadowMatrix = mShadowTransform;
        globalBuffer.CameraPos = mCamera.GetPosition3f();
        globalBuffer.SunPos = { mLightPosW.x,mLightPosW.y,mLightPosW.z };
        memcpy(data, &globalBuffer, bufferSize);
        globalCbuffer->Unmap(0, nullptr);
    }


    mCommandList->SetGraphicsRootConstantBufferView(kCommonCBV, globalCbuffer->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootConstantBufferView(kMeshConstants, meshBuffer->GetGPUVirtualAddress());
 
    for (uint32_t meshIndex = 0; meshIndex < m_PbrModel.meshes.size(); meshIndex++)
    {
        mCommandList->IASetVertexBuffers(0, 1, &m_PbrModel.meshes[meshIndex].vbv);
        mCommandList->IASetIndexBuffer(&m_PbrModel.meshes[meshIndex].ibv);
        mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        mCommandList->DrawIndexedInstanced(m_PbrModel.meshes[meshIndex].Indices.size(), 1, 0, 0, 0);
    }

    // // Draw Ground
    // ObjectConstants groundOC;
    // 
    // UINT ocSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    // XMStoreFloat4x4(&groundOC.World, m_GroundModelMatrix);
    // res->CopyData(1, groundOC);
    // mCommandList->SetGraphicsRoot32BitConstant(10, 1, 0);
    // 
    // mCommandList->SetGraphicsRootConstantBufferView(0, res->Resource()->GetGPUVirtualAddress() + ocSize);
    // mCommandList->IASetVertexBuffers(0, 1, &m_Ground.vbv);
    // mCommandList->IASetIndexBuffer(&m_Ground.ibv);
    // mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // mCommandList->DrawIndexedInstanced(m_Ground.numElements, 1, 0, 0, 0);



    // Change back to GENERIC_READ so we can read the texture in a shader.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
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
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET; // 允许同时作为 RTV 和 SRV;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_ColorBuffer)));

    // Create rtv
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = mBackBufferFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;

    m_ColorBufferRtvHandle = GetCpuHandle(mRtvHeap.Get(), 2);

    md3dDevice->CreateRenderTargetView(
        m_ColorBuffer.Get(),
        &rtvDesc,
        m_ColorBufferRtvHandle
    );

    // Create srv
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = mBackBufferFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = -1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0;

    auto srvHandle = GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ColorBufferSrv);

    md3dDevice->CreateShaderResourceView(
        m_ColorBuffer.Get(),
        &srvDesc,
        srvHandle);

}
ComPtr<ID3D12RootSignature> computeRS;
ComPtr<ID3D12PipelineState> cubePso;
ComPtr<ID3D12PipelineState> irMapPso;
ComPtr<ID3D12PipelineState> spMapPso;
ComPtr<ID3D12PipelineState> lutPso;

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
    
    ThrowIfFailed(md3dDevice->CreateRootSignature(
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
        ThrowIfFailed(md3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&cubePso)));

        ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
        mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        mCommandList->SetComputeRootSignature(computeRS.Get());
        mCommandList->SetPipelineState(cubePso.Get());
        auto srvHandle = GetGpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::ShpereMapHeap);
        mCommandList->SetComputeRootDescriptorTable(0, srvHandle);
        
        mCommandList->SetComputeRoot32BitConstant(2, 0, 0);
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMapUnfiltered.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

        for (UINT level = 0, size = 1024; level < 6; ++level, size /= 2)
        {
            auto uavHandle = GetGpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::EnvirUnfilterUavHeap + level);
            mCommandList->SetComputeRootDescriptorTable(1, uavHandle);
            const UINT numGroups = std::max<UINT>(1, size / 32);
 
            mCommandList->SetComputeRoot32BitConstant(3, level, 0);
            mCommandList->Dispatch(numGroups, numGroups, 6);
        }
        
        

        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMapUnfiltered.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));

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

        ThrowIfFailed(md3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&spMapPso)));

        // copy 0th mipMap level into destination environmentMap
        const D3D12_RESOURCE_BARRIER preCopyBarriers[] =
        {
            CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMap.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMapUnfiltered.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE)
        };
        const D3D12_RESOURCE_BARRIER postCopyBarriers[] = 
        {
            CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMap.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMapUnfiltered.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        };

        mCommandList->ResourceBarrier(2, preCopyBarriers);
        for (UINT mipLevel = 0; mipLevel < 6; mipLevel++)
        {
            for (UINT arraySlice = 0; arraySlice < 6; ++arraySlice)
            {
                const UINT subresourceIndex = D3D12CalcSubresource(mipLevel, arraySlice, 0, m_EnvirMap.Get()->GetDesc().MipLevels, 6);
                mCommandList->CopyTextureRegion(&CD3DX12_TEXTURE_COPY_LOCATION{ m_EnvirMap.Get(), subresourceIndex }, 0, 0, 0, &CD3DX12_TEXTURE_COPY_LOCATION{ m_EnvirMapUnfiltered.Get(), subresourceIndex }, nullptr);
            }
        }
        mCommandList->ResourceBarrier(2, postCopyBarriers);


        mCommandList->SetPipelineState(spMapPso.Get());
        auto srvHandle = GetGpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::EnvirSrvHeap);
        mCommandList->SetComputeRootDescriptorTable(0, srvHandle);
        
        
        const UINT levels = m_EnvirMap.Get()->GetDesc().MipLevels;
        const float deltaRoughness = 1.0f / max(float(levels - 1), 1);
        for (UINT level = 1, size = 512; level < levels; ++level, size /= 2)
        {
            
            const UINT numGroups = std::max<UINT>(1, size / 32);
            const float spmapRoughness = level * deltaRoughness;
            auto descriptor = CreateTextureUav(m_EnvirMap.Get(), level);

            auto uavHandle = GetGpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::IrradianceMapUavHeap + level);
            mCommandList->SetComputeRootDescriptorTable(1, uavHandle);

            mCommandList->SetComputeRoot32BitConstants(2, 1, &spmapRoughness, 0);
            mCommandList->Dispatch(numGroups, numGroups, 6);
        }
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMap.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
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
        ThrowIfFailed(md3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&irMapPso)));


        mCommandList->SetPipelineState(irMapPso.Get());
        auto srvHandle = GetGpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::EnvirSrvHeap);
        mCommandList->SetComputeRootDescriptorTable(0, srvHandle);
        auto uavHandle = GetGpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::IrradianceMapUavHeap);
        mCommandList->SetComputeRootDescriptorTable(1, uavHandle);
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_IrradianceMap.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        mCommandList->Dispatch(1, 1, 6);
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_IrradianceMap.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
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
        ThrowIfFailed(md3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&lutPso)));


        mCommandList->SetPipelineState(lutPso.Get());
        auto uavHandle = GetGpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::LUTuav);
        mCommandList->SetComputeRootDescriptorTable(1, uavHandle);
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_LUT.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        mCommandList->Dispatch(512/32, 512/32, 1);
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_LUT.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
    }
}

void Renderer::CreateIBL()
{

    

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

    auto descriptor = GetCpuHandle(mSrvDescriptorHeap.Get(), (int)DescriptorHeapLayout::IrradianceMapUavHeap + mipSlice);
    md3dDevice->CreateUnorderedAccessView(res, nullptr, &uavDesc, descriptor);
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


