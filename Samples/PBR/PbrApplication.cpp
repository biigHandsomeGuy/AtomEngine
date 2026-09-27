#include "pch.h"
#include "PbrApplication.h"

#include <algorithm>
#include <cwchar>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include <chrono>
#include <d3d12sdklayers.h>

#include <Imgui/imgui.h>
#include <Imgui/backends/imgui_impl_dx12.h>
#include <Imgui/backends/imgui_impl_win32.h>

#include "BufferManager.h"
#include "CommandListManager.h"
#include "Display.h"
#include "FileSystem.h"
#include "GraphicsCore.h"
#include "PathUtils.h"

using namespace Graphics;

PbrApplication::PbrApplication(std::unique_ptr<PbrApplicationUi> ui, uint32_t smokeFrameLimit)
    : m_Frames(g_CommandManager), m_Renderer(m_Scene, m_View, m_RenderResources),
      m_Ui(std::move(ui)), m_SmokeFrameLimit(smokeFrameLimit)
{
}

void PbrApplication::Startup()
{
    InitializeUi();
    m_View.Camera.SetEyeAtUp(Math::Vector3(0.0f, 0.0f, -20.0f), Math::Vector3(Math::kZero), Math::Vector3(Math::kYUnitVector));
    m_View.Camera.SetZRange(m_View.NearClip, m_View.FarClip);
    m_View.Resize(1280, 720);
    m_CameraController = std::make_unique<FlyingFPSCamera>(m_View.Camera, Math::Vector3(Math::kYUnitVector));
    m_RenderResources.Initialize(PathFromUtf8(FileSystem::GetFullPath(".")), m_Assets,
        { m_View.SceneColor.GetFormat(), m_View.SceneDepth.GetFormat(), m_View.SceneNormal.GetFormat(), m_View.Shadow.GetFormat() });
    m_Renderer.Initialize();
    auto model = ModelUtility::CreateModel(m_Assets.LoadBlocking(
        FileSystem::GetFullPath("Assets/Models/DamagedHelmet/DamagedHelmet.gltf")));
    m_Scene.AddModel(std::move(model));
    m_Ui->Initialize(m_Scene, m_Assets);
    if (m_SmokeFrameLimit != 0)
    {
        m_Ui->StartValidation(m_Scene, m_Assets);
        CheckViewIsolation();
        CheckFrameScheduler();
        StartAssetChecks();
        CheckAssetCache();
    }
}

void PbrApplication::InitializeUi()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    if (m_SmokeFrameLimit != 0)
        io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();

    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        MonitorFromWindow(GameCore::g_hWnd, MONITOR_DEFAULTTONEAREST));
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;
    io.Fonts->AddFontDefault();

    ImGui_ImplWin32_Init(GameCore::g_hWnd);
    ImGui_ImplDX12_InitInfo init;
    init.Device = g_Device;
    init.CommandQueue = g_CommandManager.GetCommandQueue();
    init.NumFramesInFlight = FrameScheduler::FramesInFlight;
    init.RTVFormat = SwapChainFormat;
    init.SrvDescriptorHeap = g_BindlessHeap.GetHeapPointer();
    init.UserData = this;
    init.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
    {
        auto& descriptors = static_cast<PbrApplication*>(info->UserData)->m_FreeUiDescriptors;
        const auto descriptor = descriptors.empty() ? g_BindlessHeap.Alloc() : descriptors.back();
        if (!descriptors.empty()) descriptors.pop_back();
        *cpu = descriptor;
        *gpu = descriptor;
    };
    init.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
    {
        static_cast<PbrApplication*>(info->UserData)->m_FreeUiDescriptors.emplace_back(cpu, gpu);
    };
    ImGui_ImplDX12_Init(&init);
    for (auto& descriptor : m_ViewportDescriptors)
        descriptor = g_BindlessHeap.Alloc();
    m_UiInitialized = true;
}

void PbrApplication::Cleanup()
{
    m_Frames.Drain();
    const auto uiValidation = m_Ui->Validation();
    if (m_SmokeFrameLimit >= 60 && m_AssetChecks != 6)
        ++m_AssetErrors;
    if (m_SmokeFrameLimit != 0 && m_CacheChecks != 12) ++m_CacheErrors;
    m_Ui->Shutdown();
    if (m_UiInitialized)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_UiInitialized = false;
    }
    m_Scene.ReplaceModels({});
    m_Assets.Shutdown();
    m_Renderer.Shutdown();
    if (m_SecondRenderer) m_SecondRenderer->Shutdown();
    m_SecondRenderer.reset();
    m_SecondView.Destroy();
    m_RenderResources.Shutdown();
    m_CameraController.reset();
    m_View.Destroy();
    if (m_SmokeFrameLimit != 0)
    {
        uint64_t validationErrors = 0;
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
        const bool validationAvailable = SUCCEEDED(g_Device->QueryInterface(IID_PPV_ARGS(&messages)));
        if (validationAvailable)
        {
            const uint64_t count = messages->GetNumStoredMessagesAllowedByRetrievalFilter();
            for (uint64_t i = 0; i < count; ++i)
            {
                SIZE_T size = 0;
                messages->GetMessage(i, nullptr, &size);
                std::vector<unsigned char> storage(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                if (SUCCEEDED(messages->GetMessage(i, message, &size)) &&
                    (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR || message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION))
                {
                    if (++validationErrors <= 10)
                        std::cerr << message->pDescription << '\n';
                }
            }
        }
        const bool deviceRemoved = FAILED(g_Device->GetDeviceRemovedReason());
        if (m_SmokeFrameLimit >= 60 && (m_ResizeChecks != 4 || m_WindowResizes != 1))
            ++m_ViewErrors;
        if (m_SecondViewFrames != m_CompletedFrames || m_MultiViewChecks != 4) ++m_ViewErrors;
        if (m_FrameChecks != 5 || (m_SmokeFrameLimit >= 60 && m_PresentationChecks != 2) ||
            m_Frames.Stats().Submissions != m_CompletedFrames ||
            m_Frames.Stats().SlotReuses != (m_CompletedFrames > FrameScheduler::FramesInFlight ?
                m_CompletedFrames - FrameScheduler::FramesInFlight : 0) ||
            m_Frames.FrameOpen() || m_Frames.PendingFrames() != 0 || m_Frames.RetainedResources() != 0)
            ++m_FrameErrors;
        m_ExitCode = validationErrors != 0 || deviceRemoved || m_ViewErrors != 0 || m_AssetErrors != 0 || m_CacheErrors != 0 ||
            m_FrameErrors != 0 || uiValidation.Errors != 0 || uiValidation.Completed != uiValidation.Expected ||
            m_CompletedFrames < m_SmokeFrameLimit ? 1 : 0;
        std::cout << "PBR_SMOKE frames=" << m_CompletedFrames << " errors=" << validationErrors
            << " deviceRemoved=" << deviceRemoved << " validation=" << validationAvailable
            << " viewErrors=" << m_ViewErrors << " resizeChecks=" << m_ResizeChecks
            << " windowResizes=" << m_WindowResizes << " assetErrors=" << m_AssetErrors
            << " assetChecks=" << m_AssetChecks << " uiErrors=" << uiValidation.Errors
            << " uiChecks=" << uiValidation.Completed << " uiExpected=" << uiValidation.Expected
            << " frameErrors=" << m_FrameErrors << " frameChecks=" << m_FrameChecks
            << " submissions=" << m_Frames.Stats().Submissions << " slotReuses=" << m_Frames.Stats().SlotReuses
            << " drained=" << (m_Frames.PendingFrames() == 0 && m_Frames.RetainedResources() == 0)
            << " presentationChecks=" << m_PresentationChecks << " multiViewChecks=" << m_MultiViewChecks
            << " secondViewFrames=" << m_SecondViewFrames
            << " cacheErrors=" << m_CacheErrors << " cacheChecks=" << m_CacheChecks << '\n';
    }
}

void PbrApplication::Update(float deltaTime)
{
    m_Assets.Update();
    if (m_SmokeFrameLimit != 0)
        PollAssetChecks();
    m_View.FarClip = std::max(m_View.FarClip, m_View.NearClip + 0.01f);
    m_View.Camera.SetZRange(m_View.NearClip, m_View.FarClip);
    m_CameraController->Update(deltaTime);
    m_View.Camera.Update();
    if (m_SmokeFrameLimit >= 60 && m_CompletedFrames == 22)
        SetWindowPos(GameCore::g_hWnd, nullptr, 0, 0, 900, 700, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (m_SmokeFrameLimit >= 60 && m_CompletedFrames == 30) ShowWindow(GameCore::g_hWnd, SW_SHOWMINNOACTIVE);
    if (m_SmokeFrameLimit >= 60 && m_CompletedFrames == 33) ShowWindow(GameCore::g_hWnd, SW_SHOWNOACTIVATE);
}

void PbrApplication::OnResize()
{
    // Presentation resize must not recreate the scene view or invalidate history.
    if (m_SmokeFrameLimit >= 60 && m_CompletedFrames == 23)
    {
        ++m_WindowResizes;
        if (m_View.Width() != 641 || m_View.Height() != 359 || !m_View.HistoryValid)
            ++m_ViewErrors;
    }
    if (m_SmokeFrameLimit >= 60 && (m_CompletedFrames == 31 || m_CompletedFrames == 34))
    {
        ++m_PresentationChecks;
        if (bool(IsIconic(GameCore::g_hWnd)) != (m_CompletedFrames == 31) ||
            m_View.Width() != 1024 || m_View.Height() != 768 || !m_View.HistoryValid)
            ++m_FrameErrors;
    }
}

void PbrApplication::RenderScene()
{
    const uint32_t frameSlot = m_Frames.BeginFrame();
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    GameCore::g_ImGuiWantsMouseCapture = ImGui::GetIO().WantTextInput;

    const DescriptorHandle& descriptor = m_ViewportDescriptors[frameSlot];
    ViewportRequest request = m_Ui->Draw(m_Scene, m_Renderer, {
        descriptor.GetGpuPtr(), m_View.Width(), m_View.Height() });
    if (m_SmokeFrameLimit != 0)
    {
        // Exercise target recreation and odd/minimum dimensions on real GPU passes.
        const ViewportRequest sizes[] = { {640, 360}, {641, 359}, {1024, 768}, {8, 8} };
        const uint32_t phase = static_cast<uint32_t>(std::min(m_CompletedFrames / 15, uint64_t(3)));
        request = sizes[phase];
        m_Renderer.PostSettings().useTAA = m_CompletedFrames < 35 || m_CompletedFrames >= 40;
    }
    if (m_Renderer.ResizeView(request.Width, request.Height) && m_SmokeFrameLimit != 0)
    {
        ++m_ResizeChecks;
        if (m_View.HistoryValid || m_View.FrameIndex != 0 || m_View.HistoryReadIndex != 0 ||
            m_View.Width() != request.Width || m_View.Height() != request.Height ||
            m_View.LinearDepthHalf.GetWidth() != (request.Width + 1) / 2 ||
            m_View.LinearDepthHalf.GetHeight() != (request.Height + 1) / 2)
            ++m_ViewErrors;
    }
    g_Device->CopyDescriptorsSimple(1, descriptor, m_View.Output.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (m_SecondRenderer && m_CompletedFrames == 5)
    {
        auto* primaryOutput = m_View.Output.GetResource();
        const bool history = m_View.HistoryValid;
        m_SecondRenderer->ResizeView(321, 179);
        ++m_MultiViewChecks;
        if (m_View.Output.GetResource() != primaryOutput || m_View.HistoryValid != history ||
            m_SecondView.HistoryValid || m_SecondView.LinearDepthHalf.GetWidth() != 161 ||
            m_SecondView.LinearDepthHalf.GetHeight() != 90) ++m_ViewErrors;
    }
    if (m_SecondRenderer && m_CompletedFrames == 10)
    {
        const auto primaryFrame = m_View.FrameIndex;
        m_SecondView.InvalidateHistory();
        ++m_MultiViewChecks;
        if (!m_View.HistoryValid || m_View.FrameIndex != primaryFrame || m_SecondView.HistoryValid ||
            m_SecondView.FrameIndex != 0) ++m_ViewErrors;
    }
    if (m_SecondRenderer && m_CompletedFrames == 50)
    {
        g_CommandManager.IdleGPU();
        const auto* signature = m_RenderResources.Pipelines.GraphicsRoot.GetSignature();
        const bool history = m_View.HistoryValid;
        m_SecondRenderer->Shutdown();
        m_SecondRenderer = std::make_unique<PbrRenderer>(m_Scene, m_SecondView, m_RenderResources);
        m_SecondRenderer->Initialize();
        m_SecondRenderer->PostSettings().useTAA = true;
        ++m_MultiViewChecks;
        if (m_RenderResources.PrecomputeCount() != 1 ||
            m_RenderResources.Pipelines.GraphicsRoot.GetSignature() != signature ||
            m_View.HistoryValid != history) ++m_ViewErrors;
    }
    ImGui::Render();

    // Geometry, compute passes and main UI share one ordered direct command list.
    GraphicsContext& graphics = GraphicsContext::Begin(L"PBR application frame");
    for (const auto& model : m_Scene.Models) m_Frames.Retain(model.GPU);
    m_Renderer.Render(graphics);
    if (m_SecondRenderer)
    {
        m_SecondRenderer->Render(graphics);
        ++m_SecondViewFrames;
        if (!m_SecondView.HistoryValid || m_SecondView.Output.GetResource() == m_View.Output.GetResource() ||
            m_SecondView.Camera.GetPosition().GetX() != 7.0f) ++m_ViewErrors;
        if (m_CompletedFrames >= 35 && m_CompletedFrames < 40 && m_View.HistoryValid) ++m_ViewErrors;
    }
    graphics.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, g_BindlessHeap.GetHeapPointer());
    graphics.TransitionResource(g_DisplayPlane[g_CurrentBuffer], D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    graphics.ClearColor(g_DisplayPlane[g_CurrentBuffer]);
    graphics.SetRenderTarget(g_DisplayPlane[g_CurrentBuffer].GetRTV());
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), graphics.GetCommandList());
    graphics.TransitionResource(g_DisplayPlane[g_CurrentBuffer], D3D12_RESOURCE_STATE_PRESENT);
    graphics.Finish(false);

    // Secondary windows may sample the scene output, so submit them afterwards.
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
    ++m_CompletedFrames;
}

void PbrApplication::OnFramePresented()
{
    // Covers the main list, font uploads, detached ImGui viewports and Present.
    m_Frames.EndFrame();
}

void PbrApplication::CheckViewIsolation()
{
    RenderView other;
    other.Resize(32, 24);
    other.HistoryValid = true;
    const auto* otherHistory = other.History[0].GetResource();
    const auto* output = m_View.Output.GetResource();
    if (m_Renderer.ResizeView(0, 0) || m_View.Output.GetResource() != output ||
        other.Output.GetResource() == output || other.History[0].GetResource() != otherHistory ||
        !other.HistoryValid || g_SceneColorBuffer.GetResource() || g_PostProcessBuffer.GetResource())
        ++m_ViewErrors;
    // Recreate another view without changing this view's resources or history.
    other.Resize(33, 25);
    if (m_View.Output.GetResource() != output || other.Width() != 33 || other.Height() != 25 || other.HistoryValid)
        ++m_ViewErrors;
    other.Destroy();
    m_SecondView.Resize(320, 180);
    m_SecondView.Camera.SetEyeAtUp(Math::Vector3(7, 3, -20), Math::Vector3(Math::kZero), Math::Vector3(Math::kYUnitVector));
    m_SecondView.Camera.SetZRange(m_SecondView.NearClip, m_SecondView.FarClip);
    m_SecondView.Camera.Update();
    m_SecondRenderer = std::make_unique<PbrRenderer>(m_Scene, m_SecondView, m_RenderResources);
    m_SecondRenderer->Initialize();
    m_SecondRenderer->PostSettings().useTAA = true;
    auto formats = Renderer::PipelineFormats{};
    formats.Color = DXGI_FORMAT_R32_FLOAT;
    bool rejected = false;
    try { m_RenderResources.Pipelines.Initialize(formats); }
    catch (const std::logic_error&) { rejected = true; }
    ++m_MultiViewChecks;
    if (!rejected || &m_Renderer.SharedResources() != &m_SecondRenderer->SharedResources() ||
        m_RenderResources.PrecomputeCount() != 1 || Graphics::g_EnvirMap.GetResource() ||
        !Renderer::s_PSOs.empty()) ++m_ViewErrors;
}

void PbrApplication::CheckFrameScheduler()
{
    auto check = [this](bool passed, const char* label)
    {
        ++m_FrameChecks;
        if (!passed)
        {
            ++m_FrameErrors;
            std::cerr << "FRAME_CHECK_FAILED " << label << '\n';
        }
    };
    // A CPU-signalled gate makes pending fences deterministic even on a fast GPU.
    // Always release it on exit so a failed check cannot leave the queue blocked.
    struct QueueGate
    {
        Microsoft::WRL::ComPtr<ID3D12Fence> Fence;
        ~QueueGate() { if (Fence) Fence->Signal(1); }
    } gate;
    try
    {
        if (FAILED(g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate.Fence))))
            throw std::runtime_error("Could not create a frame validation fence.");
        auto& queue = g_CommandManager.GetGraphicsQueue();
        FrameScheduler frames(g_CommandManager);
        auto rejects = [](auto action)
        {
            try { action(); } catch (const std::logic_error&) { return true; }
            return false;
        };
        bool protocol = rejects([&] { frames.EndFrame(); }) && rejects([&] { frames.Retain({}); });
        if (FAILED(queue.GetCommandQueue()->Wait(gate.Fence.Get(), 1)))
            throw std::runtime_error("Could not gate frame validation submissions.");
        const auto firstSlot = frames.BeginFrame();
        protocol = protocol && rejects([&] { frames.BeginFrame(); }) && rejects([&] { frames.Drain(); });
        check(protocol && firstSlot == 0, "frame begin/end protocol");

        auto first = std::make_shared<int>(1);
        std::weak_ptr<int> firstOwner = first;
        frames.Retain(first);
        frames.Retain(first);
        const bool deduplicated = frames.RetainedResources() == 1 && first.use_count() == 2;
        first.reset();
        const auto firstFence = frames.EndFrame();
        check(deduplicated && !firstOwner.expired() && !g_CommandManager.IsFenceComplete(firstFence),
            "owners retained until frame fence, with deduplication");

        const auto secondSlot = frames.BeginFrame();
        auto second = std::make_shared<int>(2);
        std::weak_ptr<int> secondOwner = second;
        frames.Retain(second);
        second.reset();
        frames.EndFrame();
        const auto thirdSlot = frames.BeginFrame();
        frames.EndFrame();
        check(secondSlot == 1 && thirdSlot == 2 && frames.PendingFrames() == 3 &&
            frames.Stats().PeakInFlight == 3 && !firstOwner.expired() && !secondOwner.expired(),
            "three queued frames use distinct slots");

        if (FAILED(gate.Fence->Signal(1))) throw std::runtime_error("Could not release frame validation gate.");
        const auto reusedSlot = frames.BeginFrame();
        check(reusedSlot == firstSlot && firstOwner.expired() && !secondOwner.expired() &&
            g_CommandManager.IsFenceComplete(firstFence) && frames.Stats().SlotReuses == 1,
            "slot reuse waits before releasing its resources");
        auto last = std::make_shared<int>(3);
        std::weak_ptr<int> lastOwner = last;
        frames.Retain(last);
        last.reset();
        frames.EndFrame();
        frames.Drain();
        check(secondOwner.expired() && lastOwner.expired() && !frames.FrameOpen() &&
            frames.PendingFrames() == 0 && frames.RetainedResources() == 0 &&
            frames.Stats().Submissions == 4 && frames.Stats().Drains == 1,
            "drain releases all owners after completion");
    }
    catch (const std::exception& error)
    {
        ++m_FrameErrors;
        std::cerr << "FRAME_CHECK_FAILED " << error.what() << '\n';
    }
}

void PbrApplication::StartAssetChecks()
{
    const auto root = PathFromUtf8(FileSystem::GetFullPath("bin-int/smoke-assets"));
    std::filesystem::create_directories(root);
    const float vertices[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    std::ofstream binary(root / "triangle.bin", std::ios::binary | std::ios::trunc);
    binary.write(reinterpret_cast<const char*>(vertices), sizeof(vertices));
    binary.close();
    const std::string triangle = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"triangle.bin","byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"mode":4}]}]})";
    std::ofstream(root / "cancel-before.gltf") << triangle;
    std::ofstream(root / "cancel-after.gltf") << triangle;
    std::string badBounds = triangle;
    badBounds.replace(badBounds.find("\"count\":3"), 9, "\"count\":4");
    std::ofstream(root / "invalid-bounds.gltf") << badBounds;
    std::ofstream(root / "invalid-json.gltf") << "{ invalid";

    const std::string cubePath = FileSystem::GetFullPath("Assets/Models/cube.glb");
    m_AssetRequests[0] = m_Assets.Request(cubePath);
    if (m_Assets.Request(FileSystem::GetFullPath("Assets/Models/./cube.glb")) != m_AssetRequests[0])
        ++m_AssetErrors;
    m_AssetRequests[1] = m_Assets.Request(PathToUtf8(root / "missing.gltf"));
    m_AssetRequests[2] = m_Assets.Request(PathToUtf8(root / "invalid-bounds.gltf"));
    m_AssetRequests[3] = m_Assets.Request(PathToUtf8(root / "invalid-json.gltf"));
    m_AssetRequests[4] = m_Assets.Request(PathToUtf8(root / "cancel-before.gltf"));
    if (!m_Assets.Cancel(m_AssetRequests[4])) ++m_AssetErrors;
    m_AssetRequests[5] = m_Assets.Request(PathToUtf8(root / "cancel-after.gltf"));

    const auto helmet = m_Assets.Snapshot(m_Assets.Request(FileSystem::GetFullPath(
        "Assets/Models/DamagedHelmet/./DamagedHelmet.gltf")));
    if (helmet.State != AssetLoadState::Ready || helmet.Data.Asset != m_Scene.Models.front().Asset ||
        helmet.Data.GPU != m_Scene.Models.front().GPU)
        ++m_AssetErrors;
}

void PbrApplication::PollAssetChecks()
{
    if (m_AssetChecks != 0) return;
    std::array<AssetLoadSnapshot, 6> jobs;
    bool finished = true;
    for (size_t index = 0; index < jobs.size(); ++index)
    {
        jobs[index] = m_Assets.Snapshot(m_AssetRequests[index]);
        auto& job = jobs[index];
        if (job.State == AssetLoadState::Uploading && job.Data.GPU) ++m_AssetErrors;
        if (index == 5 && job.State == AssetLoadState::Uploading && !m_UploadCancelRequested)
            m_UploadCancelRequested = m_Assets.Cancel(job.Id);
        if (job.State != AssetLoadState::Ready && job.State != AssetLoadState::Failed && job.State != AssetLoadState::Cancelled)
            finished = false;
    }
    if (!finished) return;
    const auto& ready = jobs[0];
    if (ready.State != AssetLoadState::Ready || !ready.Data.Asset || !ready.Data.GPU || ready.UploadFence == 0 ||
        !g_CommandManager.IsFenceComplete(ready.UploadFence))
        ++m_AssetErrors;
    else
    {
        LoadedModelData firstData = ready.Data, secondData = ready.Data;
        Model first = ModelUtility::CreateModel(std::move(firstData));
        Model second = ModelUtility::CreateModel(std::move(secondData));
        if (first.Asset != second.Asset || first.GPU != second.GPU || first.GPU->Meshes.empty())
            ++m_AssetErrors;
        first.Transform.transform.SetTranslation(Math::Vector3(5.0f, 0.0f, 0.0f));
        m_Scene.AddModel(std::move(first)); // Draw the imported residency in subsequent frames.
        m_View.InvalidateHistory();
    }
    for (size_t index = 1; index <= 3; ++index)
        if (jobs[index].State != AssetLoadState::Failed || jobs[index].Error.empty() || jobs[index].Data.GPU || jobs[index].UploadFence != 0)
            ++m_AssetErrors;
    if (jobs[4].State != AssetLoadState::Cancelled || jobs[4].UploadFence != 0 || jobs[4].Data.GPU)
        ++m_AssetErrors;
    if (!m_UploadCancelRequested || jobs[5].State != AssetLoadState::Cancelled || jobs[5].UploadFence == 0 || jobs[5].Data.GPU ||
        !g_CommandManager.IsFenceComplete(jobs[5].UploadFence))
        ++m_AssetErrors;
    m_AssetChecks = 6;
}

void PbrApplication::CheckAssetCache()
{
    auto check = [this](bool passed, const char* name)
    {
        ++m_CacheChecks;
        if (!passed)
        {
            ++m_CacheErrors;
            std::cerr << "CACHE_CHECK_FAILED " << name << '\n';
        }
    };
    try
    {
        const auto root = PathFromUtf8(FileSystem::GetFullPath("bin-int/smoke-cache"));
        std::filesystem::create_directories(root);
        const float vertices[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
        std::ofstream binary(root / "triangle.bin", std::ios::binary | std::ios::trunc);
        binary.write(reinterpret_cast<const char*>(vertices), sizeof(vertices));
        binary.close();
        const std::string fixture = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"triangle.bin","byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],"images":[{"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg=="}],"textures":[{"source":0}],"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0,"mode":4}]}]})";
        const char* names[] = { "a.gltf", "b.gltf", "c.gltf", "cancel.gltf" };
        for (const auto name : names) std::ofstream(root / name) << fixture;
        const auto aPath = PathToUtf8(root / names[0]);
        const auto bPath = PathToUtf8(root / names[1]);
        const auto cPath = PathToUtf8(root / names[2]);
        const auto baseline = g_TextureManager.Stats();
        ModelAssetManager assets;
        auto load = [&](const std::string& path)
        {
            std::string error;
            auto data = assets.LoadBlocking(path, &error);
            if (!data.Asset || !data.GPU || data.GPU->Textures.size() != 1)
                throw std::runtime_error("Cache fixture import failed: " + error);
            return data;
        };
        auto a = load(aPath);
        const uint64_t aCpu = assets.CacheStats().CpuBytes;
        auto b = load(bPath);
        const uint64_t bCpu = assets.CacheStats().CpuBytes - aCpu;
        auto c = load(cPath);
        const auto total = assets.CacheStats();
        const auto aGpu = a.GPU->ResidentBytes;
        const auto bGpu = b.GPU->ResidentBytes;
        const auto cGpu = c.GPU->ResidentBytes;
        std::weak_ptr<const ModelGPU> weakA = a.GPU, weakB = b.GPU, weakC = c.GPU;
        std::weak_ptr<const ModelAsset> weakCpuA = a.Asset;
        const auto oldTexture = a.GPU->Textures.front();
        const auto oldDescriptor = g_TextureManager.GetBindlessIndex(oldTexture);
        const auto oldMirror = g_TextureManager.GetSrvHandle(oldTexture);
        const auto oldRequest = assets.Request(aPath).Id();
        check(total.Entries == 3 && total.ReadyEntries == 3 && total.ProtectedEntries == 3 &&
            total.CpuBytes > 0 && total.GpuBytes == aGpu + bGpu + cGpu &&
            a.GPU->TextureOwners.size() == 1 && aGpu > a.GPU->TextureOwners.front()->Bytes() &&
            g_TextureManager.Stats().OwnedTextures == baseline.OwnedTextures + 3,
            "CPU estimates and committed GPU/texture allocation accounting");
        a = {}; b = {}; c = {};
        // Touch A: B is now oldest. CPU pressure must evict B alone.
        assets.Request(aPath);
        assets.SetBudget({ total.CpuBytes - bCpu, UINT64_MAX });
        auto stats = assets.CacheStats();
        check(weakB.expired() && !weakA.expired() && !weakC.expired() && stats.Evictions == 1 &&
            stats.CpuBytes == total.CpuBytes - bCpu && stats.GpuBytes == aGpu + cGpu && !stats.OverBudget(),
            "CPU budget evicts least recently used unowned entry");
        assets.SetBudget({ UINT64_MAX, aGpu });
        stats = assets.CacheStats();
        check(weakC.expired() && !weakA.expired() && stats.Evictions == 2 &&
            stats.GpuBytes == aGpu && !stats.OverBudget(), "GPU budget follows the same LRU order");
        auto cpuOwner = weakCpuA.lock();
        assets.SetBudget({ 0, 0 });
        stats = assets.CacheStats();
        check(stats.OverBudget() && stats.ProtectedEntries == 1 && !weakA.expired(),
            "an external CPU owner protects its paired residency above a soft limit");
        cpuOwner.reset();
        assets.Trim();
        bool staleRejected = false;
        try { g_TextureManager.Get(oldTexture); }
        catch (const std::out_of_range&) { staleRejected = true; }
        const bool released = weakA.expired() && assets.CacheStats().Entries == 0 &&
            g_TextureManager.Stats().LiveTextures == baseline.LiveTextures;
        auto lease = assets.Request(aPath);
        a = load(aPath);
        const auto newTexture = a.GPU->Textures.front();
        check(released && staleRejected && lease.Id() != oldRequest && newTexture != oldTexture &&
            g_TextureManager.GetBindlessIndex(newTexture) == oldDescriptor &&
            g_TextureManager.GetSrvHandle(newTexture).ptr == oldMirror.ptr &&
            g_TextureManager.Stats().DescriptorReuses > baseline.DescriptorReuses,
            "reload uses new IDs and recycles both descriptor slots without stale texture aliasing");
        weakA = a.GPU;
        a = {};
        auto secondLease = lease;
        lease = {};
        assets.Trim();
        stats = assets.CacheStats();
        check(stats.ReadyEntries == 1 && stats.ProtectedEntries == 1 && stats.OverBudget() &&
            assets.Snapshot(secondLease).State == AssetLoadState::Ready && !weakA.expired(),
            "a copied request lease protects ready data before scene commit");
        secondLease = {};
        assets.Trim();
        check(weakA.expired() && assets.CacheStats().Entries == 0 &&
            g_TextureManager.Stats().LiveTextures == baseline.LiveTextures,
            "last request release makes its cache and owned texture evictable");

        a = load(aPath);
        weakA = a.GPU;
        FrameScheduler frames(g_CommandManager);
        struct QueueGate
        {
            Microsoft::WRL::ComPtr<ID3D12Fence> Fence;
            ~QueueGate() { if (Fence) Fence->Signal(1); }
        } gate; // Releases the queue before frames/assets destruct on exceptions.
        ASSERT_SUCCEEDED(g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate.Fence)));
        ASSERT_SUCCEEDED(g_CommandManager.GetCommandQueue()->Wait(gate.Fence.Get(), 1));
        frames.BeginFrame();
        frames.Retain(a.GPU);
        frames.EndFrame();
        a = {};
        assets.Trim();
        check(frames.PendingFrames() == 1 && !weakA.expired() && assets.CacheStats().ProtectedEntries == 1 &&
            assets.CacheStats().OverBudget() && g_TextureManager.Stats().OwnedTextures == baseline.OwnedTextures + 1,
            "a pending GPU frame retains geometry, texture and descriptors during cache pressure");
        ASSERT_SUCCEEDED(gate.Fence->Signal(1));
        frames.Drain();
        assets.Trim();
        check(weakA.expired() && assets.CacheStats().GpuBytes == 0 && assets.CacheStats().Entries == 0 &&
            g_TextureManager.Stats().LiveTextures == baseline.LiveTextures,
            "frame completion releases all residency before eviction");

        gate.Fence.Reset();
        ASSERT_SUCCEEDED(g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate.Fence)));
        ASSERT_SUCCEEDED(g_CommandManager.GetCommandQueue()->Wait(gate.Fence.Get(), 1));
        auto cancelled = assets.Request(PathToUtf8(root / names[3]));
        uint64_t uploadFence = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline)
        {
            assets.Update();
            auto snapshot = assets.Snapshot(cancelled);
            if (snapshot.State == AssetLoadState::Uploading) { uploadFence = snapshot.UploadFence; break; }
            if (snapshot.State == AssetLoadState::Failed) throw std::runtime_error(snapshot.Error);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (uploadFence == 0) throw std::runtime_error("Textured upload did not reach its fence.");
        const bool didCancel = assets.Cancel(cancelled);
        cancelled = {};
        assets.Trim();
        check(didCancel && !g_CommandManager.IsFenceComplete(uploadFence) && assets.CacheStats().GpuBytes > 0 &&
            g_TextureManager.Stats().OwnedTextures == baseline.OwnedTextures + 1,
            "cancelled textured upload remains resident until its GPU fence completes");
        ASSERT_SUCCEEDED(gate.Fence->Signal(1));
        g_CommandManager.WaitForFence(uploadFence);
        assets.Update();
        check(assets.CacheStats().Entries == 0 && assets.CacheStats().CpuBytes == 0 && assets.CacheStats().GpuBytes == 0 &&
            g_TextureManager.Stats().LiveTextures == baseline.LiveTextures,
            "completed cancellation releases textures and unleased terminal diagnostics");
        const auto reuseCount = g_TextureManager.Stats().DescriptorReuses;
        TextureID previous = newTexture;
        bool cycles = true;
        for (size_t index = 0; index < 12; ++index)
        {
            auto data = load(aPath);
            const auto texture = data.GPU->Textures.front();
            cycles &= texture > previous && assets.CacheStats().ProtectedEntries == 1;
            previous = texture;
            data = {};
            assets.Trim();
            cycles &= assets.CacheStats().Entries == 0 && g_TextureManager.Stats().LiveTextures == baseline.LiveTextures;
        }
        const bool recycledEveryTime = g_TextureManager.Stats().DescriptorReuses == reuseCount + 12;
        auto shutdownLease = assets.Request(aPath);
        auto shutdownData = load(aPath);
        std::weak_ptr<const ModelGPU> shutdownOwner = shutdownData.GPU;
        shutdownData = {};
        assets.Shutdown();
        check(cycles && recycledEveryTime && shutdownLease.Id() != 0 && shutdownOwner.expired() &&
            assets.CacheStats().Entries == 0 && g_TextureManager.Stats().LiveTextures == baseline.LiveTextures,
            "repeated eviction is bounded and shutdown strips cache data from outstanding leases");
    }
    catch (const std::exception& error)
    {
        ++m_CacheErrors;
        std::cerr << "CACHE_CHECK_FAILED " << error.what() << '\n';
    }
}

bool PbrApplication::IsDone()
{
    return m_SmokeFrameLimit != 0 && m_CompletedFrames >= m_SmokeFrameLimit;
}

uint32_t PbrApplication::ParseSmokeFrameLimit(const wchar_t* commandLine)
{
    const std::wstring arguments = commandLine ? commandLine : L"";
    const std::wstring prefix = L"--smoke-frames=";
    const auto offset = arguments.find(prefix);
    if (offset == std::wstring::npos)
        return 0;
    const unsigned long limit = std::wcstoul(arguments.c_str() + offset + prefix.size(), nullptr, 10);
    return static_cast<uint32_t>(std::min(limit, 10000ul));
}
