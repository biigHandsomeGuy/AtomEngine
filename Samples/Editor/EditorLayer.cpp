#include "pch.h"
#include "EditorLayer.h"

#include <Imgui/imgui.h>
#include "..\PBR\PbrUiPanels.h"

namespace AtomEditor
{
    void EditorLayer::Initialize(Scene& scene)
    {
        AtomEditor::Initialize(m_State);
        m_State.SelectedEntity = scene.Models.empty() ? InvalidEntityId : scene.Models.front().Id;
    }

    void EditorLayer::DrawMenu(Scene& scene)
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open Scene..."))
                    OpenSceneDialog(scene, m_State, m_History);
                if (ImGui::MenuItem("Save Scene As..."))
                    SaveSceneDialog(scene, m_State);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                const std::string undoLabel = m_History.CanUndo() ? "Undo " + m_History.UndoLabel() : "Undo";
                const std::string redoLabel = m_History.CanRedo() ? "Redo " + m_History.RedoLabel() : "Redo";
                if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, m_History.CanUndo()))
                    m_History.Undo(scene);
                if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, m_History.CanRedo()))
                    m_History.Redo(scene);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                m_History.Undo(scene);
            if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                m_History.Redo(scene);
        }
        if (!scene.FindModel(m_State.SelectedEntity))
            m_State.SelectedEntity = scene.Models.empty() ? InvalidEntityId : scene.Models.front().Id;
    }

    ViewportRequest EditorLayer::Draw(Scene& scene, PbrRenderer& renderer, const ViewportImage& image)
    {
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
        const bool disabled = GameCore::g_ViewportCapturingMouse;
        if (disabled)
            ImGui::BeginDisabled();
        DrawSceneHierarchy(scene, m_State, m_History);
        DrawInspector(scene, m_State, m_History);
        DrawContentBrowser(scene, m_State, m_History);
        if (disabled)
            ImGui::EndDisabled();
        return DrawRenderViewport(image);
    }

    void EditorLayer::Shutdown()
    {
        m_History.Clear();
    }
}
