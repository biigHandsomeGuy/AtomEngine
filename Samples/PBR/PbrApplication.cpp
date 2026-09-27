#include "pch.h"
#include "PbrApplication.h"

#include <algorithm>
#include <cwchar>
#include <iostream>
#include <string>
#include <d3d12sdklayers.h>

#include <Imgui/imgui.h>
#include <Imgui/backends/imgui_impl_dx12.h>
#include <Imgui/backends/imgui_impl_win32.h>

#include "BufferManager.h"
#include "CommandListManager.h"
#include "Display.h"
#include "FileSystem.h"
#include "GraphicsCore.h"

using namespace Graphics;

PbrApplication::PbrApplication(std::unique_ptr<PbrApplicationUi> ui, uint32_t smokeFrameLimit)
    : m_Renderer(m_Scene), m_Ui(std::move(ui)), m_SmokeFrameLimit(smokeFrameLimit)
{
}

void PbrApplication::Startup()
{
    InitializeUi();
    m_Renderer.Initialize();
    auto model = ModelUtility::CreateModel(ModelUtility::LoadGltfAsset(
        FileSystem::GetFullPath("Assets/Models/DamagedHelmet/DamagedHelmet.gltf")));
    m_Scene.AddModel(std::move(model));
    m_Ui->Initialize(m_Scene);
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
    io.Fonts->Build();

    ImGui_ImplWin32_Init(GameCore::g_hWnd);
    const DescriptorHandle fontDescriptor = g_BindlessHeap.Alloc();
    ImGui_ImplDX12_Init(g_Device, 3, SwapChainFormat, g_BindlessHeap.GetHeapPointer(),
        static_cast<D3D12_CPU_DESCRIPTOR_HANDLE>(fontDescriptor),
        static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(fontDescriptor));
    for (auto& descriptor : m_ViewportDescriptors)
        descriptor = g_BindlessHeap.Alloc();
    m_UiInitialized = true;
}

void PbrApplication::Cleanup()
{
    m_Ui->Shutdown();
    if (m_UiInitialized)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_UiInitialized = false;
    }
    m_Scene.ReplaceModels({});
    m_Renderer.Shutdown();
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
        m_ExitCode = validationErrors != 0 || deviceRemoved || m_CompletedFrames < m_SmokeFrameLimit ? 1 : 0;
        std::cout << "PBR_SMOKE frames=" << m_CompletedFrames << " errors=" << validationErrors
            << " deviceRemoved=" << deviceRemoved << " validation=" << validationAvailable << '\n';
    }
}

void PbrApplication::Update(float deltaTime)
{
    m_Renderer.Update(deltaTime);
}

void PbrApplication::OnResize()
{
    m_Renderer.OnResize();
}

void PbrApplication::RenderScene()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    GameCore::g_ImGuiWantsMouseCapture = ImGui::GetIO().WantTextInput;

    const DescriptorHandle& descriptor = m_ViewportDescriptors[g_CurrentBuffer];
    g_Device->CopyDescriptorsSimple(1, descriptor, g_PostProcessBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_Ui->Draw(m_Scene, m_Renderer, {
        descriptor.GetGpuPtr(), g_PostProcessBuffer.GetWidth(), g_PostProcessBuffer.GetHeight() });
    ImGui::Render();

    // Geometry, compute passes and main UI share one ordered direct command list.
    GraphicsContext& graphics = GraphicsContext::Begin(L"PBR application frame");
    m_Renderer.Render(graphics);
    graphics.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, g_BindlessHeap.GetHeapPointer());
    graphics.TransitionResource(g_DisplayPlane[g_CurrentBuffer], D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    graphics.ClearColor(g_DisplayPlane[g_CurrentBuffer]);
    graphics.SetRenderTarget(g_DisplayPlane[g_CurrentBuffer].GetRTV());
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), graphics.GetCommandList());
    graphics.TransitionResource(g_DisplayPlane[g_CurrentBuffer], D3D12_RESOURCE_STATE_PRESENT);
    graphics.Finish(true);

    // Secondary windows may sample the scene output, so submit them afterwards.
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
    ++m_CompletedFrames;
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
