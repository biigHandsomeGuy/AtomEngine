#include "pch.h"
#include "EditorLayer.h"

#include <Imgui/imgui.h>
#include "..\PBR\PbrUiPanels.h"
#include "PathUtils.h"
#include "FileSystem.h"
#include "ViewportChecks.h"

namespace AtomEditor
{
    void EditorLayer::Initialize(Scene& scene, ModelAssetManager& assets)
    {
        AtomEditor::Initialize(m_State);
        m_Assets = &assets;
        m_ProjectRoot = PathFromUtf8(FileSystem::GetFullPath("."));
        m_DocumentLoader = std::make_unique<SceneDocumentLoader>(m_ProjectRoot);
        m_State.SelectedEntity = scene.Models.empty() ? InvalidEntityId : scene.Models.front().Id;
    }

    void EditorLayer::OpenDocument()
    {
        std::filesystem::path file;
        if (!SelectSceneFile(false, file)) return;
        CancelPendingImports(m_State, *m_Assets);
        m_OpenMaterialLab = false;
        m_DocumentLoader->Begin(file);
        m_State.StatusMessage = "Loading " + PathToUtf8(file.filename());
        m_State.StatusIsError = false;
    }

    void EditorLayer::StartValidation(const Scene& scene, ModelAssetManager& assets)
    {
        m_Checks = std::make_unique<SceneDocumentChecks>(scene, assets, m_ProjectRoot);
        m_ViewportChecks = CheckViewport(scene, m_ProjectRoot);
    }

    ApplicationValidation EditorLayer::Validation() const
    {
        auto result = m_Checks ? m_Checks->Result() : ApplicationValidation{};
        result.Errors += m_ViewportChecks.Errors;
        result.Completed += m_ViewportChecks.Completed;
        result.Expected += m_ViewportChecks.Expected;
        return result;
    }

    void EditorLayer::SaveDocument(const Scene& scene, bool saveAs)
    {
        auto file = m_DocumentPath;
        if ((saveAs || file.empty()) && !SelectSceneFile(true, file)) return;
        std::string error;
        if (SaveSceneDocument(scene, file, m_ProjectRoot, error))
        {
            m_DocumentPath = file;
            m_State.StatusMessage = "Saved " + PathToUtf8(file.filename());
            m_State.StatusIsError = false;
        }
        else
        {
            m_State.StatusMessage = std::move(error);
            m_State.StatusIsError = true;
        }
    }

    void EditorLayer::UpdateDocument(Scene& scene, PbrRenderer& renderer)
    {
        m_DocumentLoader->Update(*m_Assets);
        if (m_DocumentLoader->Apply(scene, m_History, m_State.SelectedEntity, *m_Assets))
        {
            renderer.InvalidateHistory();
            const auto status = m_DocumentLoader->Status();
            m_DocumentPath = status.File;
            renderer.SetSceneCameraSource(InvalidEntityId);
            if (m_OpenMaterialLab) { renderer.UseSceneCamera(true); renderer.ShadingSettings().UseTexture = true; }
            m_OpenMaterialLab = false;
            m_State.StatusMessage = "Opened " + PathToUtf8(status.File.filename());
            if (!status.Warnings.empty()) m_State.StatusMessage += "\n" + status.Warnings;
            m_State.StatusIsError = false;
        }
        const auto status = m_DocumentLoader->Status();
        if (status.State != m_LastLoadState)
        {
            if (status.State == SceneLoadState::Failed || status.State == SceneLoadState::Cancelled)
            {
                m_State.StatusMessage = status.State == SceneLoadState::Failed ? status.Error : "Scene load cancelled.";
                m_State.StatusIsError = status.State == SceneLoadState::Failed;
                m_OpenMaterialLab = false;
            }
            m_LastLoadState = status.State;
        }
    }

    void EditorLayer::DrawMenu(Scene& scene)
    {
        const bool editing = !m_DocumentLoader->Busy() && !GameCore::g_ViewportCapturingMouse;
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, editing)) OpenDocument();
                if (ImGui::MenuItem("Open Material Lab", nullptr, false, editing))
                {
                    CancelPendingImports(m_State, *m_Assets);
                    m_DocumentLoader->Begin(m_ProjectRoot / "Samples/Editor/Scenes/MaterialLab.atomscene");
                    m_State.StatusMessage = "Loading Material Lab";
                    m_State.StatusIsError = false;
                    m_OpenMaterialLab = true;
                }
                if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, editing)) SaveDocument(scene, false);
                if (ImGui::MenuItem("Save Scene As...", nullptr, false, editing)) SaveDocument(scene, true);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                const std::string undoLabel = m_History.CanUndo() ? "Undo " + m_History.UndoLabel() : "Undo";
                const std::string redoLabel = m_History.CanRedo() ? "Redo " + m_History.RedoLabel() : "Redo";
                if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, editing && m_History.CanUndo()))
                    m_History.Undo(scene);
                if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, editing && m_History.CanRedo()))
                    m_History.Redo(scene);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive() && !GameCore::g_ViewportCapturingMouse)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_O, false)) OpenDocument();
            if (!m_DocumentLoader->Busy() && ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveDocument(scene, io.KeyShift);
            if (!m_DocumentLoader->Busy() && ImGui::IsKeyPressed(ImGuiKey_Z, false))
                m_History.Undo(scene);
            if (!m_DocumentLoader->Busy() && ImGui::IsKeyPressed(ImGuiKey_Y, false))
                m_History.Redo(scene);
        }
        if (m_State.SelectedEntity && !scene.Contains(m_State.SelectedEntity))
            m_State.SelectedEntity = InvalidEntityId;
    }

    ViewportRequest EditorLayer::Draw(Scene& scene, PbrRenderer& renderer, const ViewportImage& image)
    {
        if (m_Checks) m_Checks->Update(*m_Assets);
        m_Viewport.BeginFrame(scene, m_History, m_DocumentLoader->Busy());
        UpdateDocument(scene, renderer);
        if (!m_DocumentLoader->Busy()) ProcessImports(scene, m_State, m_History, *m_Assets);
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("Atom Editor Workspace", nullptr, flags);
        ImGui::PopStyleVar(3);
        ImGui::DockSpace(ImGui::GetID("AtomEditorDockSpace"));
        DrawMenu(scene);
        ImGui::End();

        DrawRendererSettings(renderer);
        ImGui::Begin("Scene View");
        ImGui::BeginDisabled(GameCore::g_ViewportCapturingMouse || m_DocumentLoader->Busy());
        bool preview = renderer.UsesSceneCamera();
        if (ImGui::Checkbox("Preview scene camera", &preview)) renderer.UseSceneCamera(preview);
        const auto source = renderer.SceneCameraSource();
        const auto* camera = scene.FindCamera(source ? source : scene.ActiveCameraId());
        const auto label = source && camera ? camera->InstanceName.c_str() : "Active scene camera";
        if (ImGui::BeginCombo("Camera source", label))
        {
            if (ImGui::Selectable("Active scene camera", source == InvalidEntityId)) renderer.SetSceneCameraSource(InvalidEntityId);
            for (const auto& item : scene.Cameras)
                if (ImGui::Selectable((item.InstanceName + "##camera" + std::to_string(item.Id)).c_str(), source == item.Id))
                    renderer.SetSceneCameraSource(item.Id);
            ImGui::EndCombo();
        }
        if (preview && !camera) ImGui::TextWrapped("Camera source is unavailable; showing the editor view.");
        ImGui::EndDisabled();
        ImGui::End();
        const bool disabled = GameCore::g_ViewportCapturingMouse || m_DocumentLoader->Busy();
        if (disabled)
            ImGui::BeginDisabled();
        DrawSceneHierarchy(scene, m_State, m_History, renderer);
        DrawInspector(scene, m_State, m_History, renderer);
        DrawContentBrowser(m_State, *m_Assets);
        if (disabled)
            ImGui::EndDisabled();
        if (m_DocumentLoader->Busy())
        {
            const auto status = m_DocumentLoader->Status();
            ImGui::Begin("Scene Loading", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextWrapped("%s", PathToUtf8(status.File.filename()).c_str());
            ImGui::TextUnformatted(status.State == SceneLoadState::LoadingAssets ? "Loading model assets..." : "Reading scene document...");
            ImGui::Text("Ready models: %zu / %zu", status.ReadyModels, status.TotalModels);
            if (status.TotalModels != 0)
                ImGui::ProgressBar(float(status.ReadyModels) / float(status.TotalModels));
            if (ImGui::Button("Cancel")) m_DocumentLoader->Cancel();
            ImGui::End();
        }
        return m_Viewport.Draw(scene, m_State, m_History, renderer, image, m_DocumentLoader->Busy());
    }

    void EditorLayer::Shutdown()
    {
        m_Checks.reset();
        if (m_DocumentLoader) m_DocumentLoader->Shutdown();
        CancelPendingImports(m_State, *m_Assets);
        m_History.Clear();
    }
}
