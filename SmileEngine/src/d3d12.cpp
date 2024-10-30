#include "d3d.h"
#include "../../Core/Imgui/imgui.h"
#include "../../Core/Imgui/imgui_impl_dx12.h"
#include "../../Core/Imgui/imgui_impl_win32.h"
#include "DirectXMath.h"
#include <iostream>
#include "../../Core/stb_image/stb_image.h"
const int gNumFrameResources = 2;


D3DApp* CreateApp(HINSTANCE hInstance)
{
    return new SsaoApp(hInstance);
}

SsaoApp::SsaoApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
    
    // Estimate the scene bounding sphere manually since we know how the scene was constructed.
    // The grid is the "widest object" with a width of 20 and depth of 30.0f, and centered at
    // the world space origin.  In general, you need to loop over every world space vertex
    // position and compute the bounding sphere.
    mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
    mSceneBounds.Radius = sqrtf(10.0f*10.0f + 15.0f*15.0f);
}

SsaoApp::~SsaoApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

bool SsaoApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCamera.SetPosition(0.0f, 4.0f, -15.0f);
    
    mShadowMap = std::make_unique<ShadowMap>(md3dDevice.Get(),
        2048, 2048);

    mSsao = std::make_unique<Ssao>(
        md3dDevice.Get(),
        mCommandList.Get(),
        mClientWidth, mClientHeight);

	LoadTextures();
    BuildRootSignature();
    BuildSsaoRootSignature();
	BuildDescriptorHeaps();
    BuildShadersAndInputLayout(); 
    BuildShapeGeometry();
    CreateCubeMap();
   
    BuildFrameResources();
    BuildPSOs();

    mSsao->SetPSOs(mPSOs["ssao"].Get(), mPSOs["ssaoBlur"].Get());
    

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

void SsaoApp::CreateRtvAndDsvDescriptorHeaps()
{
    // Add +1 for screen normal map, +2 for ambient maps.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 3;
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
 
void SsaoApp::OnResize()
{
    D3DApp::OnResize();

	mCamera.SetLens(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

    if(mSsao != nullptr)
    {
        mSsao->OnResize(mClientWidth, mClientHeight);

        // Resources changed, so need to rebuild descriptors.
        mSsao->RebuildDescriptors(mDepthStencilBuffer.Get());
    }
}

void SsaoApp::Update(const GameTimer& gt)
{
    
    OnKeyboardInput(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    //
    // Animate the lights (and hence shadows).
    //

  
    UpdateShadowTransform(gt);
	UpdateMainPassCB(gt);
    UpdateShadowPassCB(gt);
    UpdateSsaoCB(gt);
}

void SsaoApp::Draw(const GameTimer& gt)
{

    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    

    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
   
	//
	// Shadow map pass.
	//

    // Bind all the materials used in this scene.  For structured buffers, we can bypass the heap and 
    // set as a root descriptor.
   
    // Bind null SRV for shadow map pass.
    mCommandList->SetGraphicsRootDescriptorTable(2, mNullSrv);	 

    // Bind all the textures used in this scene.  Observe
    // that we only have to specify the first descriptor in the table.  
    // The root signature knows how many descriptors are expected in the table.
    mCommandList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    // 生成光源角度的 shadow map
    //DrawSceneToShadowMap();

	//
	// Normal/depth pass.
	//
	
	//DrawNormalsAndDepth();
	
	//
	// Compute SSAO.
	// 
	
    //mCommandList->SetGraphicsRootSignature(mSsaoRootSignature.Get());
    //mSsao->ComputeSsao(mCommandList.Get(), mCurrFrameResource, 1);
	
	//
	// Main rendering pass.
	//
	
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

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
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Bind all the textures used in this scene.  Observe
    // that we only have to specify the first descriptor in the table.  
    // The root signature knows how many descriptors are expected in the table.
    mCommandList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	
    auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    // Bind the sky cube map.  For our demos, we just use one "world" cube map representing the environment
    // from far away, so all objects will use the same cube map and we only need to set it once per-frame.  
    // If we wanted to use "local" cube maps, we would have to change them per-object, or dynamically
    // index into an array of cube maps.

    auto skyTexDescriptor = GetGpuSrv(int(DescriptorHeapLayout::ShpereMapHeap));
    mCommandList->SetGraphicsRootDescriptorTable(2, skyTexDescriptor);

    static bool show_demo_window = false;
    static bool show_another_window = false;
    
    static ImVec4 clear_color = ImVec4(0,0,0,1);
    color = XMFLOAT4(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);
    static int b = 0;
    static bool UsePcss = true;
    static bool UseSsao = true;
    {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

        ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
        ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
        ImGui::Checkbox("Another Window", &show_another_window);

        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

        ImGui::Checkbox("UsePcss", &UsePcss);
        ImGui::Checkbox("UseSsao", &UseSsao);
        
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
    auto IrradianceMapDescriptor = GetGpuSrv(int(DescriptorHeapLayout::IrradianceMapSrvHeap));
    auto specular = GetGpuSrv(int(DescriptorHeapLayout::EnvirSrvHeap));
    auto LUT = GetGpuSrv(int(DescriptorHeapLayout::LUTsrv));

    mCommandList->SetGraphicsRootDescriptorTable(5, IrradianceMapDescriptor);
    mCommandList->SetGraphicsRootDescriptorTable(6, specular);
    mCommandList->SetGraphicsRootDescriptorTable(7, LUT);

    static float roughness = 0;
    ImGui::DragFloat("roughness", &roughness, 0.0005, 0.0, 1);
    mCommandList->SetGraphicsRoot32BitConstants(8, 1, &roughness, 0);
   
    auto cubeMapDescriptor = GetGpuSrv(int(DescriptorHeapLayout::EnvirSrvHeap));
    
    mCommandList->SetGraphicsRootDescriptorTable(4, cubeMapDescriptor);
    auto res = mCurrFrameResource->ObjectCB.get();

    ObjectConstants oc;
    XMMATRIX model;
    //model = XMMatrixScaling(0.1, 0.1, 0.1);
    //model *= XMMatrixTranslation(0, 2, 0);
    model = XMMatrixRotationX(5);
    //model *= XMMatrixRotationY(-90);
    //model = XMMatrixScaling(1,1,1);
    XMStoreFloat4x4(&oc.World, model);
    res->CopyData(0, oc);
    mCommandList->SetPipelineState(mPSOs["opaque"].Get());
    
    mCommandList->SetGraphicsRootConstantBufferView(0, res->Resource()->GetGPUVirtualAddress());
    //mCommandList->IASetVertexBuffers(0, 1, &m_PbrModel.vbv);
    //mCommandList->IASetIndexBuffer(&m_PbrModel.ibv);
    //mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    //mCommandList->DrawIndexedInstanced(m_PbrModel.numElements, 1, 0, 0, 0);
    mCommandList->IASetVertexBuffers(0, 1, &m_SkyBox.vbv);
    mCommandList->SetGraphicsRootConstantBufferView(0, res->Resource()->GetGPUVirtualAddress());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetIndexBuffer(&m_SkyBox.ibv);
    mCommandList->DrawIndexedInstanced(m_SkyBox.numElements, 1, 0, 0, 0);
    static int mips = 0;
    ImGui::InputInt("mips", &mips);
    mCommandList->SetGraphicsRoot32BitConstants(9, 1, &mips, 0);
    
    mCommandList->SetPipelineState(mPSOs["sky"].Get());
    mCommandList->IASetVertexBuffers(0, 1, &m_SkyBox.vbv);
    mCommandList->SetGraphicsRootConstantBufferView(0, res->Resource()->GetGPUVirtualAddress());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetIndexBuffer(&m_SkyBox.ibv);
    mCommandList->DrawIndexedInstanced(m_SkyBox.numElements,1,0,0,0);


    // Draw Specular Map LUT
    D3D12_VIEWPORT LUTviewPort = { 0,0,256,256,0,1 };
    D3D12_RECT LUTrect = { 0,0,256,256 };
    
    mCommandList->RSSetViewports(1, &LUTviewPort);
    mCommandList->RSSetScissorRects(1, &LUTrect);
    
    mCommandList->SetPipelineState(mPSOs["debug"].Get());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP); 
    mCommandList->DrawInstanced(4, 1, 0, 0);
  
    // RenderingF
    ImGui::Render();
    //mCommandList->SetDescriptorHeaps(1, &mSrvDescriptorHeap);
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
    ThrowIfFailed(mSwapChain->Present(1, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void SsaoApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void SsaoApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
    mouseDown = false;
}

void SsaoApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_RBUTTON) != 0)
    {
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);

        mouseDown = true;
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}
 
void SsaoApp::OnKeyboardInput(const GameTimer& gt)
{
    if (mouseDown)
    {
        const float dt = gt.DeltaTime();

        if (GetAsyncKeyState('W') & 0x8000)
            mCamera.Walk(10.0f * dt);

        if (GetAsyncKeyState('S') & 0x8000)
            mCamera.Walk(-10.0f * dt);

        if (GetAsyncKeyState('A') & 0x8000)
            mCamera.Strafe(-10.0f * dt);

        if (GetAsyncKeyState('D') & 0x8000)
            mCamera.Strafe(10.0f * dt);

        mCamera.UpdateViewMatrix();
    }
    
}
 

void SsaoApp::UpdateShadowTransform(const GameTimer& gt)
{
    // Only the first "main" light casts a shadow.
    XMVECTOR lightPos = XMLoadFloat4(&XMFLOAT4(mMainPassCB.Lights[0].Position.x, mMainPassCB.Lights[0].Position.y, mMainPassCB.Lights[0].Position.z,1.0f));
    XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
    XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

    XMStoreFloat3(&mLightPosW, lightPos);

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

    mLightNearZ = n;
    mLightFarZ = f;
    XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

    // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);

    XMMATRIX S = lightView*lightProj*T;
    XMStoreFloat4x4(&mLightView, lightView);
    XMStoreFloat4x4(&mLightProj, lightProj);
    XMStoreFloat4x4(&mShadowTransform, S);
}

void SsaoApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);

    XMMATRIX viewProjTex = XMMatrixMultiply(viewProj, T);
    XMMATRIX shadowTransform = XMLoadFloat4x4(&mShadowTransform);

	XMStoreFloat4x4(&mMainPassCB.View, view);
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, proj);
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, viewProj);
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProjTex, XMMatrixTranspose(viewProjTex));
    XMStoreFloat4x4(&mMainPassCB.ShadowTransform, XMMatrixTranspose(shadowTransform));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.4f, 0.4f, 0.6f, 1.0f };
	mMainPassCB.Lights[0].Position = XMFLOAT3(-15,0,-2);
	mMainPassCB.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };
	mMainPassCB.Lights[1].Position = XMFLOAT3(-6, 2, 0);
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB.Lights[2].Position = XMFLOAT3(-7, 0, 3);
	mMainPassCB.Lights[2].Strength = { 0.3f, 0.3f, 0.3f };
 
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void SsaoApp::UpdateShadowPassCB(const GameTimer& gt)
{
    XMMATRIX view = XMLoadFloat4x4(&mLightView);
    XMMATRIX proj = XMLoadFloat4x4(&mLightProj);

    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    UINT w = mShadowMap->Width();
    UINT h = mShadowMap->Height();

    XMStoreFloat4x4(&mShadowPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mShadowPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mShadowPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mShadowPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mShadowPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mShadowPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    mShadowPassCB.EyePosW = mLightPosW;
    mShadowPassCB.RenderTargetSize = XMFLOAT2((float)w, (float)h);
    mShadowPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / w, 1.0f / h);
    mShadowPassCB.NearZ = mLightNearZ;
    mShadowPassCB.FarZ = mLightFarZ;

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(1, mShadowPassCB);
}

void SsaoApp::UpdateSsaoCB(const GameTimer& gt)
{
    SsaoConstants ssaoCB;

    XMMATRIX P = mCamera.GetProj();

    // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);

    ssaoCB.Proj    = mMainPassCB.Proj;
    ssaoCB.InvProj = mMainPassCB.InvProj;
    XMStoreFloat4x4(&ssaoCB.ProjTex, XMMatrixTranspose(P*T));

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
 
    auto currSsaoCB = mCurrFrameResource->SsaoCB.get();
    currSsaoCB->CopyData(0, ssaoCB);
}

void SsaoApp::LoadTextures()
{
	std::vector<std::string> texNames = 
	{
        "albedo",
        "normal",
        "metallic",
        "roughness",

		"skyCubeMap"
	};
	
    std::vector<std::string> texFilenames =
    {
        "D:/SmileEngine/Assets/Textures/Cerberus_by_Andrew_Maximov/albedo.tga",
        "D:/SmileEngine/Assets/Textures/Cerberus_by_Andrew_Maximov/normal.tga",
        "D:/SmileEngine/Assets/Textures/Cerberus_by_Andrew_Maximov/metallic.tga",
        "D:/SmileEngine/Assets/Textures/Cerberus_by_Andrew_Maximov/roughness.tga",
        "D:/SmileEngine/Assets/Textures/EnvirMap/environment.hdr"
    };
    //stbi_set_flip_vertically_on_load(true);
	for(int i = 0; i < (int)texNames.size(); ++i)
	{
        if (i == (int)texNames.size() - 1)
            stbi_set_flip_vertically_on_load(false);

        auto texMap = std::make_unique<Texture>();
        texMap->Name = texNames[i];
        texMap->Filename = texFilenames[i];

        int width = 0, height = 0, channels = 0;
        
        UCHAR* imageData = stbi_load(texMap->Filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!imageData) {
            // 错误处理
            assert(1);
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
}

void SsaoApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 3, 0);

    CD3DX12_DESCRIPTOR_RANGE texTable2;
	texTable2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 13, 0);

    CD3DX12_DESCRIPTOR_RANGE texTable3;
    texTable3.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 14, 0);

    CD3DX12_DESCRIPTOR_RANGE texTable4;
    texTable4.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 15, 0);

    CD3DX12_DESCRIPTOR_RANGE texTable5;
    texTable5.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 16, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[10];

	// Perfomance TIP: Order from most frequent to least frequent.
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable2, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[5].InitAsDescriptorTable(1, &texTable3, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[6].InitAsDescriptorTable(1, &texTable4, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[7].InitAsDescriptorTable(1, &texTable5, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[8].InitAsConstants(1, 2, 0);
    slotRootParameter[9].InitAsConstants(1, 3, 0);

	auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(10, slotRootParameter,
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

void SsaoApp::BuildSsaoRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable0;
    texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

    CD3DX12_DESCRIPTOR_RANGE texTable1;
    texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

    // Perfomance TIP: Order from most frequent to least frequent.
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstants(1, 1);
    slotRootParameter[2].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[3].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
        0.0f,
        0,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE); 

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
    {
        pointClamp, linearClamp, depthMapSam, linearWrap
    };

    // A root signature is an array of root parameters.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
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
        IID_PPV_ARGS(mSsaoRootSignature.GetAddressOf())));
}

void SsaoApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 64;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	std::vector<ComPtr<ID3D12Resource>> tex2DList = 
	{
		mTextures["albedo"]->Resource,
		mTextures["normal"]->Resource,
		mTextures["metallic"]->Resource,
		mTextures["roughness"]->Resource
	};
	
	auto skyCubeMap = mTextures["skyCubeMap"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	
	for(UINT i = 0; i < (UINT)tex2DList.size(); ++i)
	{
		srvDesc.Format = tex2DList[i]->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = tex2DList[i]->GetDesc().MipLevels;
		md3dDevice->CreateShaderResourceView(tex2DList[i].Get(), &srvDesc, hDescriptor);

		// next descriptor
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	}
	
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = skyCubeMap->GetDesc().MipLevels;
	srvDesc.Format = skyCubeMap->GetDesc().Format;
    // ShpereMap
	md3dDevice->CreateShaderResourceView(skyCubeMap.Get(), &srvDesc, hDescriptor);


    auto nullSrv = GetCpuSrv((int)DescriptorHeapLayout::NullCubeCbvHeap);
    mNullSrv = GetGpuSrv((int)DescriptorHeapLayout::NullCubeCbvHeap);

    md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
    nullSrv.Offset(1, mCbvSrvUavDescriptorSize);

    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

    nullSrv.Offset(1, mCbvSrvUavDescriptorSize);
    md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

    mShadowMap->BuildDescriptors(
        GetCpuSrv((int)DescriptorHeapLayout::ShadowMapHeap),
        GetGpuSrv((int)DescriptorHeapLayout::ShadowMapHeap),
        GetDsv(1));

    // create 5 srv
    mSsao->BuildDescriptors(
        mDepthStencilBuffer.Get(),
        GetCpuSrv((int)DescriptorHeapLayout::SsaoMapHeap),
        GetGpuSrv((int)DescriptorHeapLayout::SsaoMapHeap),
        GetRtv(SwapChainBufferCount),
        mCbvSrvUavDescriptorSize,
        mRtvDescriptorSize);


    // Create environment Unfilter Map 
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

        auto envirSrv = GetCpuSrv((int)DescriptorHeapLayout::EnvirUnfilterSrvHeap);

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
            auto envirUav = GetCpuSrv((int)DescriptorHeapLayout::EnvirUnfilterUavHeap + i);

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

        auto envirSrv = GetCpuSrv((int)DescriptorHeapLayout::EnvirSrvHeap);

        md3dDevice->CreateShaderResourceView(m_EnvirMap.Get(), &srvDesc, envirSrv);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = skyCubeMap->GetDesc().Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.ArraySize = 6;
        uavDesc.Texture2DArray.FirstArraySlice = 0;
        uavDesc.Texture2DArray.MipSlice = 0;
        uavDesc.Texture2DArray.PlaneSlice = 0;
        auto envirUav = GetCpuSrv((int)DescriptorHeapLayout::EnvirUavHeap);

        md3dDevice->CreateUnorderedAccessView(m_EnvirMap.Get(), nullptr, &uavDesc, envirUav);

    }

    // Create irradiance Map 
    {
        D3D12_RESOURCE_DESC irMapDesc = {};
        irMapDesc.Width = 32;
        irMapDesc.Height = 32;
        irMapDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        irMapDesc.Format = skyCubeMap->GetDesc().Format;
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
        IrMapSrvDesc.Format = skyCubeMap->GetDesc().Format;
        IrMapSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        IrMapSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        IrMapSrvDesc.TextureCube.MipLevels = -1;
        IrMapSrvDesc.TextureCube.MostDetailedMip = 0;
        IrMapSrvDesc.TextureCube.ResourceMinLODClamp = 0;

        auto irMapSrv = GetCpuSrv((int)DescriptorHeapLayout::IrradianceMapSrvHeap);

        md3dDevice->CreateShaderResourceView(m_IrradianceMap.Get(), &IrMapSrvDesc, irMapSrv);

        D3D12_UNORDERED_ACCESS_VIEW_DESC IrMapUavDesc = {};
        IrMapUavDesc.Format = skyCubeMap->GetDesc().Format;
        IrMapUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        IrMapUavDesc.Texture2DArray.ArraySize = 6;
        IrMapUavDesc.Texture2DArray.FirstArraySlice = 0;
        IrMapUavDesc.Texture2DArray.MipSlice = 0;
        IrMapUavDesc.Texture2DArray.PlaneSlice = 0;
        auto irMapUav = GetCpuSrv((int)DescriptorHeapLayout::IrradianceMapUavHeap);

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

        auto spbrdfSrv = GetCpuSrv((int)DescriptorHeapLayout::LUTsrv);

        md3dDevice->CreateShaderResourceView(m_LUT.Get(), &srvDesc, spbrdfSrv);


        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        uavDesc.Texture2D.PlaneSlice = 0;
        

        auto spbrdfUrv = GetCpuSrv((int)DescriptorHeapLayout::LUTuav);
        md3dDevice->CreateUnorderedAccessView(m_LUT.Get(), nullptr, &uavDesc, spbrdfUrv);
    }
}

void SsaoApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};
    const D3D_SHADER_MACRO pcssDefines[] =
    {
        "PCSS", "1",
        NULL, NULL
    };
    const D3D_SHADER_MACRO ssaoDefines[] =
    {
        "SSAO", "1",
        NULL, NULL
    };


	mShaders["standardVS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/Default.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["opaquePS_PCSS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/Default.hlsl", pcssDefines, "PS", "ps_5_1");
	mShaders["opaquePS_SSAO"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/Default.hlsl", ssaoDefines, "PS", "ps_5_1");

    mShaders["shadowVS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/Shadows.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["shadowOpaquePS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/Shadows.hlsl", nullptr, "PS", "ps_5_1");
    mShaders["shadowAlphaTestedPS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/Shadows.hlsl", alphaTestDefines, "PS", "ps_5_1");
	
    mShaders["debugVS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/TextureDebug.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["debugPS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/TextureDebug.hlsl", nullptr, "PS", "ps_5_1");

    mShaders["drawNormalsVS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/DrawNormals.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["drawNormalsPS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/DrawNormals.hlsl", nullptr, "PS", "ps_5_1");

    mShaders["ssaoVS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/Ssao.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["ssaoPS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/Ssao.hlsl", nullptr, "PS", "ps_5_1");

    mShaders["ssaoBlurVS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/SsaoBlur.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["ssaoBlurPS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/SsaoBlur.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["skyVS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/Sky.hlsl", nullptr, "PS", "ps_5_1");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    //mInputLayout_Pos_UV =
    //{
    //    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	//	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    //};
}

void SsaoApp::BuildShapeGeometry()
{
  
    m_SkyBox = CreateMeshBuffer(Mesh::fromFile(std::string("D:/SmileEngine/Assets/Models/cube.fbx")));
    m_PbrModel = CreateMeshBuffer(Mesh::fromFile(std::string("D:/SmileEngine/Assets/Models/Cerberus_LP.FBX")));

}

void SsaoApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;

	
    ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    basePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    basePsoDesc.pRootSignature = mRootSignature.Get();
    basePsoDesc.VS =
	{ 
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), 
		mShaders["standardVS"]->GetBufferSize()
	};
    basePsoDesc.PS =
	{ 
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
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

    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePcssPsoDesc = opaquePsoDesc;
    opaquePcssPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["opaquePS_PCSS"]->GetBufferPointer()),
        mShaders["opaquePS_PCSS"]->GetBufferSize()
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePcssPsoDesc, IID_PPV_ARGS(&mPSOs["opaquePcss"])));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueSsaoPsoDesc = opaquePsoDesc;
    opaqueSsaoPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["opaquePS_SSAO"]->GetBufferPointer()),
        mShaders["opaquePS_SSAO"]->GetBufferSize()
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueSsaoPsoDesc, IID_PPV_ARGS(&mPSOs["opaqueSsao"])));


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
        reinterpret_cast<BYTE*>(mShaders["shadowVS"]->GetBufferPointer()),
        mShaders["shadowVS"]->GetBufferSize()
    };
    smapPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["shadowOpaquePS"]->GetBufferPointer()),
        mShaders["shadowOpaquePS"]->GetBufferSize()
    };
    
    // Shadow map pass does not have a render target.
    smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    smapPsoDesc.NumRenderTargets = 0;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));

    //
    // PSO for debug layer.
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = basePsoDesc;
    debugPsoDesc.pRootSignature = mRootSignature.Get();
    debugPsoDesc.InputLayout = { nullptr, 0};
    debugPsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["debugVS"]->GetBufferPointer()),
        mShaders["debugVS"]->GetBufferSize()
    };
    debugPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["debugPS"]->GetBufferPointer()),
        mShaders["debugPS"]->GetBufferSize()
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"])));

    //
    // PSO for drawing normals.
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC drawNormalsPsoDesc = basePsoDesc;
    drawNormalsPsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["drawNormalsVS"]->GetBufferPointer()),
        mShaders["drawNormalsVS"]->GetBufferSize()
    };
    drawNormalsPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["drawNormalsPS"]->GetBufferPointer()),
        mShaders["drawNormalsPS"]->GetBufferSize()
    };
    drawNormalsPsoDesc.RTVFormats[0] = Ssao::NormalMapFormat;
    drawNormalsPsoDesc.SampleDesc.Count = 1;
    drawNormalsPsoDesc.SampleDesc.Quality = 0;
    drawNormalsPsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&drawNormalsPsoDesc, IID_PPV_ARGS(&mPSOs["drawNormals"])));
    
    //
    // PSO for SSAO.  
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoPsoDesc = basePsoDesc;
    ssaoPsoDesc.InputLayout = { nullptr, 0 };
    ssaoPsoDesc.pRootSignature = mSsaoRootSignature.Get();
    ssaoPsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["ssaoVS"]->GetBufferPointer()),
        mShaders["ssaoVS"]->GetBufferSize()
    };
    ssaoPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["ssaoPS"]->GetBufferPointer()),
        mShaders["ssaoPS"]->GetBufferSize()
    };

    // SSAO effect does not need the depth buffer.
    ssaoPsoDesc.DepthStencilState.DepthEnable = false;
    ssaoPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ssaoPsoDesc.RTVFormats[0] = Ssao::AmbientMapFormat;
    ssaoPsoDesc.SampleDesc.Count = 1;
    ssaoPsoDesc.SampleDesc.Quality = 0;
    ssaoPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ssaoPsoDesc, IID_PPV_ARGS(&mPSOs["ssao"])));

    //
    // PSO for SSAO blur.
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoBlurPsoDesc = ssaoPsoDesc;
    ssaoBlurPsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["ssaoBlurVS"]->GetBufferPointer()),
        mShaders["ssaoBlurVS"]->GetBufferSize()
    };
    ssaoBlurPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["ssaoBlurPS"]->GetBufferPointer()),
        mShaders["ssaoBlurPS"]->GetBufferSize()
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ssaoBlurPsoDesc, IID_PPV_ARGS(&mPSOs["ssaoBlur"])));

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
		reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
		mShaders["skyVS"]->GetBufferSize()
	};
	skyPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
		mShaders["skyPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));

}

void SsaoApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            2, 1));
    }
}

void SsaoApp::DrawSceneToShadowMap()
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

    // Bind the pass constant buffer for the shadow map pass.
    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
    auto passCB = mCurrFrameResource->PassCB->Resource();
    D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1*passCBByteSize;
    mCommandList->SetGraphicsRootConstantBufferView(1, passCBAddress);

    mCommandList->SetPipelineState(mPSOs["shadow_opaque"].Get());


    // Change back to GENERIC_READ so we can read the texture in a shader.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
}
 
void SsaoApp::DrawNormalsAndDepth()
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
    auto passCB = mCurrFrameResource->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    mCommandList->SetPipelineState(mPSOs["drawNormals"].Get());


    // Change back to GENERIC_READ so we can read the texture in a shader.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
}
ComPtr<ID3D12RootSignature> computeRS;
ComPtr<ID3D12PipelineState> cubePso;
ComPtr<ID3D12PipelineState> irMapPso;
ComPtr<ID3D12PipelineState> spMapPso;
ComPtr<ID3D12PipelineState> lutPso;

void SsaoApp::CreateCubeMap()
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
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&computeRS)));


    // create cube map compute pso

    {
        mShaders["EnvirToCubeMap"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/SphereMapToCubeMap.hlsl", nullptr, "main", "cs_5_1");
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.CS =
        {
            reinterpret_cast<BYTE*>(mShaders["EnvirToCubeMap"]->GetBufferPointer()),
            mShaders["EnvirToCubeMap"]->GetBufferSize()
        };
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        psoDesc.pRootSignature = computeRS.Get();
        ThrowIfFailed(md3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&cubePso)));

        ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
        mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        //mCommandList->SetDescriptorHeaps(1, &mSrvDescriptorHeap);
        mCommandList->SetComputeRootSignature(computeRS.Get());
        mCommandList->SetPipelineState(cubePso.Get());
        auto srvHandle = GetGpuSrv((int)DescriptorHeapLayout::ShpereMapHeap);
        mCommandList->SetComputeRootDescriptorTable(0, srvHandle);
        
        mCommandList->SetComputeRoot32BitConstant(2, 0, 0);
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMapUnfiltered.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

        for (UINT level = 0, size = 1024; level < 6; ++level, size /= 2)
        {
            auto uavHandle = GetGpuSrv((int)DescriptorHeapLayout::EnvirUnfilterUavHeap + level);
            mCommandList->SetComputeRootDescriptorTable(1, uavHandle);
            const UINT numGroups = std::max<UINT>(1, size / 32);
 
            mCommandList->SetComputeRoot32BitConstant(3, level, 0);
            mCommandList->Dispatch(numGroups, numGroups, 6);
        }
        
        

        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMapUnfiltered.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));

    }

    // Compute pre-filtered specular environment map
    {
        mShaders["SpecularMap"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/SpecularMap.hlsl", nullptr, "main", "cs_5_1");

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.CS =
        {
            reinterpret_cast<BYTE*>(mShaders["SpecularMap"]->GetBufferPointer()),
            mShaders["SpecularMap"]->GetBufferSize()
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
        auto srvHandle = GetGpuSrv((int)DescriptorHeapLayout::EnvirSrvHeap);
        mCommandList->SetComputeRootDescriptorTable(0, srvHandle);
        
        
        const UINT levels = m_EnvirMap.Get()->GetDesc().MipLevels;
        const float deltaRoughness = 1.0f / max(float(levels - 1), 1);
        for (UINT level = 1, size = 512; level < levels; ++level, size /= 2)
        {
            
            const UINT numGroups = std::max<UINT>(1, size / 32);
            const float spmapRoughness = level * deltaRoughness;
            auto descriptor = CreateTextureUav(m_EnvirMap.Get(), level);

            auto uavHandle = GetGpuSrv((int)DescriptorHeapLayout::IrradianceMapUavHeap + level);
            mCommandList->SetComputeRootDescriptorTable(1, uavHandle);

            mCommandList->SetComputeRoot32BitConstants(2, 1, &spmapRoughness, 0);
            mCommandList->Dispatch(numGroups, numGroups, 6);
        }
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_EnvirMap.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
    }

    

    // create irradiance map compute pso
    {
        mShaders["IrradianceMap"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/IrradianceMap.hlsl", nullptr, "main", "cs_5_1");
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.CS =
        {
            reinterpret_cast<BYTE*>(mShaders["IrradianceMap"]->GetBufferPointer()),
            mShaders["IrradianceMap"]->GetBufferSize()
        };
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        psoDesc.pRootSignature = computeRS.Get();
        ThrowIfFailed(md3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&irMapPso)));


        mCommandList->SetPipelineState(irMapPso.Get());
        auto srvHandle = GetGpuSrv((int)DescriptorHeapLayout::EnvirSrvHeap);
        mCommandList->SetComputeRootDescriptorTable(0, srvHandle);
        auto uavHandle = GetGpuSrv((int)DescriptorHeapLayout::IrradianceMapUavHeap);
        mCommandList->SetComputeRootDescriptorTable(1, uavHandle);
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_IrradianceMap.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        mCommandList->Dispatch(1, 1, 6);
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_IrradianceMap.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
    }


    {
        mShaders["spbrdf"] = d3dUtil::CompileShader(L"D:/SmileEngine/Assets/Shaders/spbrdf.hlsl", nullptr, "main", "cs_5_1");
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.CS =
        {
            reinterpret_cast<BYTE*>(mShaders["spbrdf"]->GetBufferPointer()),
            mShaders["spbrdf"]->GetBufferSize()
        };
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        psoDesc.pRootSignature = computeRS.Get();
        ThrowIfFailed(md3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&lutPso)));


        mCommandList->SetPipelineState(lutPso.Get());
        auto uavHandle = GetGpuSrv((int)DescriptorHeapLayout::LUTuav);
        mCommandList->SetComputeRootDescriptorTable(1, uavHandle);
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_LUT.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        mCommandList->Dispatch(512/32, 512/32, 1);
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_LUT.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
    }
}

void SsaoApp::CreateIBL()
{



}

D3D12_CPU_DESCRIPTOR_HANDLE SsaoApp::CreateTextureUav(ID3D12Resource* res, UINT mipSlice)
{
    auto desc = res->GetDesc();

    assert(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = desc.Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    uavDesc.Texture2DArray.MipSlice = mipSlice;
    uavDesc.Texture2DArray.FirstArraySlice = 0;
    uavDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;

    auto descriptor = GetCpuSrv((int)DescriptorHeapLayout::IrradianceMapUavHeap + mipSlice);
    md3dDevice->CreateUnorderedAccessView(res, nullptr, &uavDesc, descriptor);
    return descriptor;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE SsaoApp::GetCpuSrv(int index)const
{
    auto srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    srv.Offset(index, mCbvSrvUavDescriptorSize);
    return srv;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE SsaoApp::GetGpuSrv(int index)const
{
    auto srv = CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    srv.Offset(index, mCbvSrvUavDescriptorSize);
    return srv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE SsaoApp::GetDsv(int index)const
{
    auto dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
    dsv.Offset(index, mDsvDescriptorSize);
    return dsv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE SsaoApp::GetRtv(int index)const
{
    auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtv.Offset(index, mRtvDescriptorSize);
    return rtv;
}

MeshBuffer SsaoApp::CreateMeshBuffer(const std::unique_ptr<Mesh>& mesh)
{
    MeshBuffer buffer;
    buffer.numElements = mesh->faces().size() * 3;

    const size_t vertexDataSize = mesh->vertices().size() * sizeof(Mesh::Vertex);
    const size_t indexDataSize = mesh->faces().size() * sizeof(Mesh::Face);

    // create GPU resource
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(vertexDataSize),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&buffer.vertexBuffer)
    ));

    buffer.vbv.BufferLocation = buffer.vertexBuffer->GetGPUVirtualAddress();
    buffer.vbv.SizeInBytes = static_cast<UINT>(vertexDataSize);
    buffer.vbv.StrideInBytes = sizeof(Mesh::Vertex);

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES{ D3D12_HEAP_TYPE_UPLOAD },
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(indexDataSize),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&buffer.indexBuffer)));

    buffer.ibv.BufferLocation = buffer.indexBuffer->GetGPUVirtualAddress();
    buffer.ibv.SizeInBytes = static_cast<UINT>(indexDataSize);
    buffer.ibv.Format = DXGI_FORMAT_R32_UINT;

    UINT8* data = nullptr;
    // copy vertex
    ThrowIfFailed(buffer.vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data)));
    memcpy(data, &mesh->vertices()[0], vertexDataSize);
    buffer.vertexBuffer->Unmap(0, nullptr);

    // copy index
    buffer.indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
    memcpy(data, mesh->faces().data(), indexDataSize);
    buffer.indexBuffer->Unmap(0, nullptr);

    return buffer;
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> SsaoApp::GetStaticSamplers()
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


