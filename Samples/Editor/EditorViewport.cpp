#include "pch.h"
#include "EditorViewport.h"

#include <Imgui/imgui.h>
#include <ImGuizmo/ImGuizmo.h>
#include <cmath>

namespace AtomEditor
{
    namespace
    {
        float Distance(Math::Vector3 a, Math::Vector3 b) { return XMVectorGetX(XMVector3Length(a - b)); }

        void DrawBounds(const Math::Camera& camera, const AABB& bounds, const ImVec2& origin, const ImVec2& size)
        {
            XMFLOAT3 lo, hi;
            XMStoreFloat3(&lo, bounds.GetMin()); XMStoreFloat3(&hi, bounds.GetMax());
            auto corner = [&](int i) { return Math::Vector3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z); };
            auto* draw = ImGui::GetWindowDrawList();
            for (int i = 0; i < 8; ++i)
                for (int bit = 1; bit <= 4; bit *= 2)
                    if (!(i & bit))
                    {
                        XMFLOAT2 a, b;
                        if (ProjectEdge(camera, corner(i), corner(i | bit), a, b))
                            draw->AddLine(ImVec2(origin.x + a.x * size.x, origin.y + a.y * size.y),
                                ImVec2(origin.x + b.x * size.x, origin.y + b.y * size.y), IM_COL32(255, 190, 70, 255), 1.5f);
                    }
        }
    }

    void EditorViewport::Release(Scene& scene, CommandHistory& history)
    {
        m_Drag.Commit(scene, history);
        m_NavigationButton = -1;
        ImGuizmo::Enable(false);
        GameCore::g_ViewportCapturingMouse = false;
    }

    void EditorViewport::BeginFrame(Scene& scene, CommandHistory& history, bool blocked)
    {
        ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
        ImGuizmo::BeginFrame();
        if (blocked) Release(scene, history);
        if (m_Drag.Active() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            m_Drag.Cancel(scene);
            ImGuizmo::Enable(false);
        }
        if (m_Drag.Active() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_Drag.Commit(scene, history);
            ImGuizmo::Enable(false);
        }
        if (m_NavigationButton >= 0 && !ImGui::IsMouseDown(m_NavigationButton)) m_NavigationButton = -1;
        GameCore::g_ViewportCapturingMouse = Capturing();
        GameCore::g_ViewportHovered = false;
    }

    void EditorViewport::TakeCamera(PbrRenderer& renderer, const Math::Camera& camera)
    {
        renderer.UseSceneCamera(false);
        // The host updated its camera before UI. Publish the updated matrices as
        // well as the pose so this frame's renderer matches picking and handles.
        renderer.NavigationCamera() = camera;
        renderer.CameraNearClip() = camera.GetNearClip();
        renderer.CameraFarClip() = camera.GetFarClip();
    }

    void EditorViewport::UpdatePivot(const Scene& scene, EntityId selected, const Math::Camera& camera)
    {
        const auto bounds = SceneQueries::EntityBounds(scene, selected);
        if (!m_HasPivot || selected != m_PivotSelection)
        {
            if (bounds) m_Pivot = bounds->GetCenter();
            else m_Pivot = camera.GetPosition() + camera.GetForwardVec() * 10;
            m_SelectionCenter = bounds ? bounds->GetCenter() : Math::Vector3(0, 0, 0);
            m_HasPivot = true; m_PivotSelection = selected;
        }
        else if (bounds)
        {
            m_Pivot += bounds->GetCenter() - m_SelectionCenter;
            m_SelectionCenter = bounds->GetCenter();
        }
    }

    void EditorViewport::Navigate(PbrRenderer& renderer, Math::Camera& camera, float height, bool hovered)
    {
        const auto& io = ImGui::GetIO();
        if (m_NavigationButton < 0 && hovered && !ImGui::IsAnyItemActive())
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { m_NavigationButton = ImGuiMouseButton_Right; m_Orbit = false; }
            else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) { m_NavigationButton = ImGuiMouseButton_Middle; m_Orbit = false; }
            else if (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { m_NavigationButton = ImGuiMouseButton_Left; m_Orbit = true; }
        }
        bool changed = false;
        if (m_NavigationButton >= 0 && ImGui::IsMouseDown(m_NavigationButton))
        {
            if (m_Orbit) OrbitCamera(camera, m_Pivot, io.MouseDelta.x, io.MouseDelta.y);
            else if (m_NavigationButton == ImGuiMouseButton_Middle) PanCamera(camera, m_Pivot, io.MouseDelta.x, io.MouseDelta.y, height);
            else
            {
                const float distance = std::max(1.0f, Distance(camera.GetPosition(), m_Pivot));
                LookCamera(camera, io.MouseDelta.x, io.MouseDelta.y);
                Math::Vector3 motion(0, 0, 0);
                if (!io.KeyCtrl && !io.KeyAlt)
                {
                    if (ImGui::IsKeyDown(ImGuiKey_W)) motion += camera.GetForwardVec();
                    if (ImGui::IsKeyDown(ImGuiKey_S)) motion -= camera.GetForwardVec();
                    if (ImGui::IsKeyDown(ImGuiKey_D)) motion += camera.GetRightVec();
                    if (ImGui::IsKeyDown(ImGuiKey_A)) motion -= camera.GetRightVec();
                    if (ImGui::IsKeyDown(ImGuiKey_E)) motion += Math::Vector3(Math::kYUnitVector);
                    if (ImGui::IsKeyDown(ImGuiKey_Q)) motion -= Math::Vector3(Math::kYUnitVector);
                }
                if (XMVectorGetX(XMVector3LengthSq(motion)) > 0)
                    camera.SetPosition(camera.GetPosition() + Math::Normalize(motion) *
                        (std::clamp(io.DeltaTime, 0.0f, 0.05f) * distance * 0.6f * (io.KeyShift ? 4 : 1)));
                m_Pivot = camera.GetPosition() + camera.GetForwardVec() * distance;
            }
            changed = true;
        }
        if (hovered && m_NavigationButton < 0 && io.MouseWheel != 0 && !ImGui::IsAnyItemActive())
        {
            DollyCamera(camera, m_Pivot, io.MouseWheel); changed = true;
        }
        if (changed) { camera.Update(); TakeCamera(renderer, camera); }
    }

    ViewportRequest EditorViewport::Draw(Scene& scene, UiState& state, CommandHistory& history,
        PbrRenderer& renderer, const ViewportImage& image, bool blocked)
    {
        ViewportRequest request;
        if (ImGui::Begin("ViewPort", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            request = DrawContent(scene, state, history, renderer, image, blocked);
        else { m_ImageRect = {}; Release(scene, history); }
        ImGui::End();
        return request;
    }

    ViewportRequest EditorViewport::DrawContent(Scene& scene, UiState& state, CommandHistory& history,
        PbrRenderer& renderer, const ViewportImage& image, bool blocked)
    {
        const auto& io = ImGui::GetIO();
        if (m_Drag.Active() && m_Drag.Entity() != state.SelectedEntity) Release(scene, history);
        const bool selected = scene.Contains(state.SelectedEntity);
        ImGui::BeginDisabled(blocked || Capturing());
        if (ImGui::RadioButton("Move (W)", m_Operation == Operation::Translate)) m_Operation = Operation::Translate;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate (E)", m_Operation == Operation::Rotate)) m_Operation = Operation::Rotate;
        ImGui::SameLine();
        ImGui::BeginDisabled(!scene.FindModel(state.SelectedEntity));
        if (ImGui::RadioButton("Scale (R)", m_Operation == Operation::Scale)) m_Operation = Operation::Scale;
        ImGui::EndDisabled();
        ImGui::SameLine(); ImGui::Checkbox("Local", &m_ToolSettings.Local);
        ImGui::SameLine(); ImGui::Checkbox("Snap", &m_ToolSettings.Snap);
        if (m_ToolSettings.Snap)
        {
            ImGui::SetNextItemWidth(100);
            float* step = m_Operation == Operation::Translate ? &m_ToolSettings.TranslationStep : m_Operation == Operation::Rotate ? &m_ToolSettings.RotationStep : &m_ToolSettings.ScaleStep;
            ImGui::DragFloat("Step", step, m_Operation == Operation::Scale ? 0.01f : 0.1f, 0.001f, 1000, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::BeginDisabled(!selected);
        bool focus = ImGui::Button("Focus (F)");
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::TextDisabled("Alt+LMB orbit | MMB pan | wheel zoom | RMB + WASDQE fly");
        ImGui::EndDisabled();

        const auto size = ImGui::GetContentRegionAvail();
        const auto origin = ImGui::GetCursorScreenPos();
        ViewportRequest request;
        if (!image.TextureId || size.x < 8 || size.y < 8)
        {
            m_ImageRect = {}; Release(scene, history); return request;
        }
        request.Width = static_cast<uint32_t>(std::floor(size.x)); request.Height = static_cast<uint32_t>(std::floor(size.y));
        m_ImageRect = {origin.x, origin.y, size.x, size.y};
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + size.x, origin.y + size.y);
        draw->AddImage(static_cast<ImTextureID>(image.TextureId), origin, end);
        // Layout without a button ID so ImGuizmo can activate over the image.
        ImGui::Dummy(size);
        const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(origin, end);
        GameCore::g_ViewportHovered = hovered && !blocked;
        Math::Camera camera = ViewportCamera(scene, renderer, size.y / size.x);
        UpdatePivot(scene, state.SelectedEntity, camera);
        const bool shortcuts = hovered && !blocked && !Capturing() && !io.WantTextInput && !ImGui::IsAnyItemActive() &&
            !io.KeyCtrl && !io.KeyAlt && !io.MouseDown[ImGuiMouseButton_Right] && !io.MouseDown[ImGuiMouseButton_Middle];
        if (shortcuts)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_Operation = Operation::Translate;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_Operation = Operation::Rotate;
            if (scene.FindModel(state.SelectedEntity) && ImGui::IsKeyPressed(ImGuiKey_R, false)) m_Operation = Operation::Scale;
            focus |= ImGui::IsKeyPressed(ImGuiKey_F, false);
        }
        if (focus && !blocked && !Capturing())
            if (const auto bounds = SceneQueries::EntityBounds(scene, state.SelectedEntity))
                if (FocusCamera(camera, *bounds, size.y / size.x, m_Pivot))
                {
                    camera.Update(); TakeCamera(renderer, camera); m_HasPivot = true;
                }
        if (!blocked && !io.WantTextInput && !m_Drag.Active()) Navigate(renderer, camera, size.y, hovered);
        draw->PushClipRect(origin, end, true);
        if (const auto bounds = SceneQueries::EntityBounds(scene, state.SelectedEntity)) DrawBounds(camera, *bounds, origin, size);
        ImGuizmo::SetDrawlist(draw);
        ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::Enable(!blocked && m_NavigationButton < 0 && !io.KeyAlt && !io.WantTextInput && (hovered || m_Drag.Active()));
        bool over = false;
        if (auto* transform = scene.EntityTransform(state.SelectedEntity))
        {
            XMFLOAT4X4 view, projection, matrix;
            XMStoreFloat4x4(&view, camera.GetViewMatrix()); XMStoreFloat4x4(&projection, camera.GetProjMatrix());
            XMStoreFloat4x4(&matrix, transform->GetMatrix());
            auto operation = m_Operation;
            if (operation == Operation::Scale && !scene.FindModel(state.SelectedEntity)) operation = Operation::Translate;
            const float step = operation == Operation::Translate ? m_ToolSettings.TranslationStep : operation == Operation::Rotate ? m_ToolSettings.RotationStep : m_ToolSettings.ScaleStep;
            const float snap[] = {step, step, step};
            const auto id = std::to_string(state.SelectedEntity);
            ImGuizmo::PushID(id.c_str());
            // Hide axis scale handles. The center handle scales all axes equally.
            ImGuizmo::SetAxisMask(operation == Operation::Scale, operation == Operation::Scale, operation == Operation::Scale);
            const bool changed = ImGuizmo::Manipulate(&view._11, &projection._11,
                operation == Operation::Translate ? ImGuizmo::TRANSLATE : operation == Operation::Rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE,
                m_ToolSettings.Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD, &matrix._11, nullptr, m_ToolSettings.Snap ? snap : nullptr);
            over = ImGuizmo::IsOver();
            if (ImGuizmo::IsUsing() && !m_Drag.Active()) m_Drag.Begin(scene, state.SelectedEntity);
            if (changed && m_Drag.Active()) m_Drag.Apply(scene, matrix);
            if (m_Drag.Active() && !ImGuizmo::IsUsing()) m_Drag.Commit(scene, history);
            ImGuizmo::SetAxisMask(false, false, false);
            ImGuizmo::PopID();
        }
        draw->PopClipRect();
        if (!blocked && !io.WantTextInput && !m_Drag.Active())
        {
            if (hovered && m_NavigationButton < 0 && !over && !ImGuizmo::IsUsing() && !ImGui::IsAnyItemActive() &&
                !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const auto ray = SceneQueries::ViewRay(camera, (io.MousePos.x - origin.x) / size.x, (io.MousePos.y - origin.y) / size.y);
                state.SelectedEntity = ray ? SceneQueries::PickModel(scene, *ray) : InvalidEntityId;
                m_HasPivot = false;
            }
        }
        GameCore::g_ViewportCapturingMouse = Capturing();
        return request;
    }
}
