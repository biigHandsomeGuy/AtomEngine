#include "pch.h"
#include "ViewportChecks.h"
#include "EditorViewport.h"
#include "SceneDocument.h"
#include <Imgui/imgui.h>
#include <ImGuizmo/ImGuizmo.h>
#include <cmath>
#include <iostream>

namespace AtomEditor
{
    namespace
    {
        bool Near(float a, float b, float tolerance = 0.001f) { return std::abs(a - b) < tolerance; }
        float VecLength(Math::Vector3 v) { return XMVectorGetX(XMVector3Length(v)); }
        XMFLOAT4X4 Matrix(const TransformComponent& transform)
        {
            XMFLOAT4X4 result; XMStoreFloat4x4(&result, transform.GetMatrix()); return result;
        }
        Model Triangle()
        {
            auto asset = std::make_shared<ModelAsset>();
            MeshAsset mesh;
            for (const auto& point : {Math::Vector3(-1, -1, 0), Math::Vector3(1, -1, 0), Math::Vector3(0, 1, 0)})
            {
                Vertex vertex{}; vertex.Position = point; mesh.Vertices.push_back(vertex); mesh.Bounds.AddPoint(point);
            }
            mesh.Indices = {0, 1, 2};
            SubMesh sub; sub.IndexCount = 3; sub.Bounds = mesh.Bounds; mesh.SubMeshes.push_back(sub);
            asset->Bounds = mesh.Bounds; asset->Meshes.push_back(std::move(mesh));
            Model model; model.Asset = asset; model.GPU = std::make_shared<ModelGPU>();
            model.InstanceName = "Picking triangle"; return model;
        }
    }

    ApplicationValidation CheckViewport(const Scene& source, const std::filesystem::path& root)
    {
        ApplicationValidation result{0, 0, 40};
        auto check = [&](bool passed, const char* label) {
            ++result.Completed;
            if (!passed) { ++result.Errors; std::cerr << "VIEWPORT_CHECK_FAILED " << label << '\n'; }
        };
        bool importedBounds = !source.Models.empty();
        for (const auto& model : source.Models)
        {
            importedBounds &= model.Asset && SceneQueries::ValidBounds(model.Asset->Bounds);
            if (model.Asset)
                for (const auto& mesh : model.Asset->Meshes)
                {
                    importedBounds &= SceneQueries::ValidBounds(mesh.Bounds);
                    for (const auto& sub : mesh.SubMeshes) importedBounds &= SceneQueries::ValidBounds(sub.Bounds);
                }
        }
        check(importedBounds, "decoded model/mesh/primitive bounds");
        Scene scene;
        const auto first = scene.AddModel(Triangle()).Id;
        Math::Camera camera;
        camera.SetEyeAtUp(Math::Vector3(0, 0, 5), Math::Vector3(0, 0, 0), Math::Vector3(Math::kYUnitVector));
        camera.SetPerspectiveMatrix((XM_PI / 3), 0.75f, 0.1f, 100);
        camera.Update();
        auto ray = SceneQueries::ViewRay(camera, 0.5f, 0.5f);
        check(ray && SceneQueries::PickModel(scene, *ray) == first, "center ray triangle hit");
        check(!SceneQueries::ViewRay(camera, -0.01f, 0.5f) && !SceneQueries::ViewRay(camera, NAN, 0), "outside and nonfinite coordinates");
        const SceneQueries::Ray empty{Math::Vector3(0, 0, 5), Math::Normalize(Math::Vector3(0.9f, 0.9f, -5)), 0, 100};
        check(SceneQueries::PickModel(scene, empty) == InvalidEntityId, "bounds hit without triangle hit");
        auto nearer = Triangle(); nearer.Transform.transform.SetTranslation(Math::Vector3(0, 0, 1));
        const auto second = scene.AddModel(std::move(nearer)).Id;
        check(SceneQueries::PickModel(scene, *ray) == second, "nearest instance wins");
        auto clipped = *ray; clipped.MinDistance = 4.5f;
        check(SceneQueries::PickModel(scene, clipped) == first, "near plane rejects nearer hit");
        clipped.MaxDistance = 4.9f;
        check(SceneQueries::PickModel(scene, clipped) == InvalidEntityId, "far plane rejects remaining hit");
        scene.RemoveModel(second);
        scene.FindModel(first)->Transform.scale = 2;
        scene.FindModel(first)->Transform.transform.SetRotation(Math::Quaternion(0, XM_PIDIV4, 0));
        scene.FindModel(first)->Transform.transform.SetTranslation(Math::Vector3(3, 0, 0));
        check(SceneQueries::PickModel(scene, {Math::Vector3(3, 0, 5), Math::Vector3(0, 0, -1), 0, 100}) == first &&
            SceneQueries::WorldBounds(*scene.FindModel(first))->GetDimensions().GetX() > 2.8f, "rotated scaled translated instance");
        scene.FindModel(first)->Transform = {};
        check(!SceneQueries::ValidBounds(AABB{}) && !SceneQueries::EntityBounds(scene, 0), "empty bounds and selection");

        const AABB box(Math::Vector3(-4, -2, -1), Math::Vector3(4, 2, 1));
        Math::Vector3 pivot;
        bool framed = true;
        for (float aspect : {0.3f, 3.0f})
        {
            framed &= FocusCamera(camera, box, aspect, pivot); camera.Update();
            for (int i = 0; i < 8; ++i)
            {
                XMFLOAT2 p, q;
                framed &= ProjectEdge(camera, Math::Vector3(i & 1 ? 4.0f : -4.0f, i & 2 ? 2.0f : -2.0f, i & 4 ? 1.0f : -1.0f),
                    Math::Vector3(i & 1 ? 4.0f : -4.0f, i & 2 ? 2.0f : -2.0f, i & 4 ? 1.0f : -1.0f), p, q) &&
                    p.x > 0 && p.x < 1 && p.y > 0 && p.y < 1;
            }
        }
        check(framed, "focus fits wide and tall viewports");
        const float distance = VecLength(camera.GetPosition() - pivot);
        OrbitCamera(camera, pivot, 30, 10000);
        check(Near(VecLength(camera.GetPosition() - pivot), distance) && std::abs(float(camera.GetForwardVec().GetY())) < 1, "orbit radius and pole clamp");
        const auto delta = camera.GetPosition() - pivot;
        PanCamera(camera, pivot, 35, -17, 600);
        check(VecLength((camera.GetPosition() - pivot) - delta) < 0.001f, "pan preserves relative pivot");
        DollyCamera(camera, pivot, 1);
        check(VecLength(camera.GetPosition() - pivot) < distance && VecLength(camera.GetPosition() - pivot) > camera.GetNearClip(), "wheel dolly");
        camera.SetEyeAtUp(Math::Vector3(0, 0, 5), Math::Vector3(0, 0, 0), Math::Vector3(Math::kYUnitVector)); camera.Update();
        XMFLOAT2 p, q;
        check(ProjectEdge(camera, Math::Vector3(0, 0, 5), Math::Vector3(0, 0, 0), p, q) &&
            !ProjectEdge(camera, Math::Vector3(0, 0, 6), Math::Vector3(0, 0, 7), p, q), "bounds edge near-plane clipping");

        CommandHistory history;
        TransformDrag drag;
        auto changed = scene.FindModel(first)->Transform;
        changed.transform.SetTranslation(Math::Vector3(1, 2, 3));
        const auto revision = scene.Revision();
        drag.Begin(scene, first); drag.Apply(scene, Matrix(changed));
        changed.transform.SetTranslation(Math::Vector3(2, 3, 4)); drag.Apply(scene, Matrix(changed));
        const bool committed = drag.Commit(scene, history);
        const bool undone = history.Undo(scene) && !history.CanUndo() && Near(float(scene.FindModel(first)->Transform.transform.GetTranslation().GetX()), 0);
        check(committed && undone && history.Redo(scene) && scene.Revision() > revision &&
            Near(float(scene.FindModel(first)->Transform.transform.GetTranslation().GetX()), 2), "one gesture one command with undo/redo");
        history.Clear(); drag.Begin(scene, first); drag.Apply(scene, Matrix(changed));
        check(!drag.Commit(scene, history) && !history.CanUndo(), "no-op creates no history");
        drag.Begin(scene, first); auto cancelled = changed; cancelled.scale = 3;
        drag.Apply(scene, Matrix(cancelled)); drag.Cancel(scene);
        check(Near(scene.FindModel(first)->Transform.scale, 1) && !history.CanUndo(), "escape cancels entire transform");
        drag.Begin(scene, first);
        XMFLOAT4X4 invalid; XMStoreFloat4x4(&invalid, XMMatrixScaling(1, 2, 1));
        bool rejected = !drag.Apply(scene, invalid);
        invalid = Matrix(changed); invalid._12 = 0.5f; rejected &= !drag.Apply(scene, invalid);
        invalid = Matrix(changed); invalid._11 = NAN; rejected &= !drag.Apply(scene, invalid);
        XMStoreFloat4x4(&invalid, XMMatrixScaling(-1, 1, 1)); rejected &= !drag.Apply(scene, invalid);
        check(rejected && !drag.Commit(scene, history), "nonuniform shear nonfinite reflection rejected");
        changed.scale = 2.5f; changed.transform.SetRotation(Math::Quaternion(0.2f, 0.7f, 0.1f));
        drag.Begin(scene, first); drag.Apply(scene, Matrix(changed)); drag.Commit(scene, history);
        check(Near(scene.FindModel(first)->Transform.scale, 2.5f) && history.Undo(scene) && history.Redo(scene), "rotation and uniform scale retain transform");
        CameraEntity cameraEntity; cameraEntity.Id = (EntityId(1) << 40) + 1;
        const auto cameraId = scene.AddCamera(cameraEntity).Id;
        auto cameraPose = scene.FindCamera(cameraId)->Transform; cameraPose.transform.SetTranslation(Math::Vector3(7, 8, 9));
        history.Clear(); drag.Begin(scene, cameraId); drag.Apply(scene, Matrix(cameraPose)); drag.Commit(scene, history);
        check(history.Undo(scene) && history.Redo(scene) && Near(float(scene.FindCamera(cameraId)->Transform.transform.GetTranslation().GetX()), 7), "64-bit camera transform identity");
        drag.Begin(scene, cameraId); cameraPose.scale = 2;
        check(!drag.Apply(scene, Matrix(cameraPose)) && !drag.Commit(scene, history), "camera scaling is rejected");
        const auto file = root / "bin-int/smoke-documents/viewport.atomscene";
        std::filesystem::create_directories(file.parent_path());
        std::string error;
        // Use a real source so the saved transform can be opened by the document loader.
        Scene saved;
        auto persistent = ModelUtility::CreateModel(LoadedModelData{source.Models.front().Asset, source.Models.front().GPU});
        persistent.Transform = changed; saved.AddModel(std::move(persistent));
        check(SaveSceneDocument(saved, file, root, error), "viewport transform serialized through document service");

        // Software input drives the actual ImGui viewport and pinned ImGuizmo.
        // It is isolated from the application context and never submits fake GPU handles.
        auto* originalContext = ImGui::GetCurrentContext();
        const bool oldHovered = GameCore::g_ViewportHovered, oldCaptured = GameCore::g_ViewportCapturingMouse;
        auto* context = ImGui::CreateContext();
        ImGui::SetCurrentContext(context);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.DisplaySize = ImVec2(900, 700); io.DeltaTime = 1.0f / 60;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels; int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height); io.Fonts->SetTexID(ImTextureID(1));
        Scene uiScene; const auto uiId = uiScene.AddModel(Triangle()).Id;
        UiState state; CommandHistory uiHistory; EditorViewport viewport;
        RenderView view; PbrResources resources; PbrRenderer renderer(uiScene, view, resources);
        view.EditorCamera.SetEyeAtUp(Math::Vector3(0, 0, 5), Math::Vector3(0, 0, 0), Math::Vector3(Math::kYUnitVector));
        view.NearClip = 0.1f; view.FarClip = 100;
        view.EditorCamera.SetZRange(0.1f, 100); view.EditorCamera.Update();
        bool uiDrawing = true;
        auto frame = [&](float x, float y, bool left, bool right = false, bool middle = false, bool blocked = false) {
            io.AddMousePosEvent(x, y); io.AddMouseButtonEvent(0, left); io.AddMouseButtonEvent(1, right); io.AddMouseButtonEvent(2, middle);
            ImGui::NewFrame(); viewport.BeginFrame(uiScene, uiHistory, blocked);
            ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Viewport checks", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            viewport.DrawContent(uiScene, state, uiHistory, renderer, {1, 900, 700}, blocked);
            ImGui::End(); ImGui::Render();
            uiDrawing &= ImGui::GetDrawData()->TotalVtxCount > 0;
        };
        frame(-100, -100, false); frame(450, 380, false);
        auto rect = viewport.ImageRect();
        const float cx = rect.x + rect.z * 0.5f, cy = rect.y + rect.w * 0.5f;
        frame(cx, cy, false); frame(cx, cy, true); frame(cx, cy, false);
        check(state.SelectedEntity == uiId, "mouse click selects and syncs hierarchy state");
        frame(rect.x + 5, rect.y + 5, false); frame(rect.x + 5, rect.y + 5, true); frame(rect.x + 5, rect.y + 5, false);
        check(state.SelectedEntity == InvalidEntityId, "blank click deselects");
        state.SelectedEntity = uiId;
        frame(cx, cy, false); frame(cx, cy, true);
        const bool dragging = viewport.Capturing();
        frame(cx + 40, cy + 20, true); frame(cx + 60, cy + 30, true); frame(cx + 60, cy + 30, false);
        const auto moved = uiScene.FindModel(uiId)->Transform.transform.GetTranslation();
        check(dragging && VecLength(moved) > 0.05f && uiHistory.CanUndo(), "real translation gizmo mouse drag");
        check(uiHistory.Undo(uiScene) && !uiHistory.CanUndo() && VecLength(uiScene.FindModel(uiId)->Transform.transform.GetTranslation()) < 0.001f &&
            uiHistory.Redo(uiScene) && VecLength(uiScene.FindModel(uiId)->Transform.transform.GetTranslation() - moved) < 0.001f,
            "real gizmo gesture has one undo/redo record");
        uiHistory.Undo(uiScene); uiHistory.Clear();
        frame(cx, cy, false); frame(cx, cy, true); frame(cx + 30, cy + 10, true);
        io.AddKeyEvent(ImGuiKey_Escape, true); frame(cx + 30, cy + 10, true);
        io.AddKeyEvent(ImGuiKey_Escape, false); frame(cx + 30, cy + 10, false);
        check(!uiHistory.CanUndo() && VecLength(uiScene.FindModel(uiId)->Transform.transform.GetTranslation()) < 0.001f, "real escape cancellation");
        io.AddKeyEvent(ImGuiKey_R, true); frame(cx, cy, false);
        io.AddKeyEvent(ImGuiKey_R, false); frame(cx, cy, false); frame(cx, cy, true); frame(cx + 50, cy, true); frame(cx + 50, cy, false);
        check(viewport.Tool() == EditorViewport::Operation::Scale && Near(uiScene.FindModel(uiId)->Transform.scale, 1.5f) && uiHistory.CanUndo(),
            "R shortcut and real uniform scale handle");
        uiHistory.Undo(uiScene); uiHistory.Clear();
        io.AddKeyEvent(ImGuiKey_E, true); frame(cx, cy, false);
        io.AddKeyEvent(ImGuiKey_E, false); frame(cx, cy, false);
        float ring = 0;
        for (float radius = 10; radius < 150 && !ring; radius += 2)
        {
            frame(cx + radius, cy, false);
            if (ImGuizmo::IsOver(ImGuizmo::ROTATE_Z)) ring = radius;
        }
        if (ring) { frame(cx + ring, cy, true); frame(cx, cy + ring, true); frame(cx, cy + ring, false); }
        XMFLOAT4 rotation; XMStoreFloat4(&rotation, uiScene.FindModel(uiId)->Transform.transform.GetRotation());
        check(ring > 0 && std::abs(rotation.z) > 0.1f && uiHistory.CanUndo(), "E shortcut and real rotation ring");
        if (uiHistory.CanUndo()) uiHistory.Undo(uiScene);
        uiHistory.Clear();
        // Local rotation uses the same stable transaction; snap reaches the library.
        viewport.Settings().Local = true; viewport.Settings().Snap = true; viewport.Settings().TranslationStep = 0.5f;
        io.AddKeyEvent(ImGuiKey_W, true); frame(cx, cy, false);
        io.AddKeyEvent(ImGuiKey_W, false); frame(cx, cy, false);
        rect = viewport.ImageRect();
        const float snapCx = rect.x + rect.z * 0.5f, snapCy = rect.y + rect.w * 0.5f;
        frame(snapCx, snapCy, false); frame(snapCx, snapCy, true); frame(snapCx + 73, snapCy + 37, true); frame(snapCx + 73, snapCy + 37, false);
        const auto snapped = uiScene.FindModel(uiId)->Transform.transform.GetTranslation();
        check(viewport.Tool() == EditorViewport::Operation::Translate && VecLength(snapped) > 0.01f &&
            Near(float(snapped.GetX()) / 0.5f, std::round(float(snapped.GetX()) / 0.5f)) &&
            Near(float(snapped.GetY()) / 0.5f, std::round(float(snapped.GetY()) / 0.5f)), "configured local gizmo snapping");
        if (uiHistory.CanUndo()) uiHistory.Undo(uiScene);
        uiHistory.Clear(); viewport.Settings().Local = false; viewport.Settings().Snap = false;
        io.AddKeyEvent(ImGuiKey_F, true); frame(cx, cy, false);
        io.AddKeyEvent(ImGuiKey_F, false); frame(cx, cy, false);
        check(VecLength(view.EditorCamera.GetPosition()) < 5 && !uiHistory.CanUndo(), "F focuses selection without scene command");
        const auto beforeZoom = view.EditorCamera.GetPosition();
        io.AddMouseWheelEvent(0, 1); frame(cx + 150, cy + 120, false);
        check(VecLength(view.EditorCamera.GetPosition()) < VecLength(beforeZoom), "real wheel navigation");
        const auto beforePan = view.EditorCamera.GetPosition();
        frame(cx + 150, cy + 120, false, false, true); frame(cx + 180, cy + 135, false, false, true); frame(cx + 180, cy + 135, false);
        check(VecLength(view.EditorCamera.GetPosition() - beforePan) > 0.01f && !viewport.Capturing(), "real middle-button pan and release");
        const auto beforeFly = view.EditorCamera.GetPosition();
        frame(cx + 150, cy + 120, false, true); io.AddKeyEvent(ImGuiKey_W, true); frame(cx + 160, cy + 125, false, true);
        io.AddKeyEvent(ImGuiKey_W, false); frame(cx + 160, cy + 125, false);
        check(VecLength(view.EditorCamera.GetPosition() - beforeFly) > 0.001f && !viewport.Capturing(), "RMB fly owns W navigation");
        check(XMVectorGetX(XMVector3Length(XMVector3TransformCoord(view.EditorCamera.GetPosition(), view.EditorCamera.GetViewMatrix()))) < 0.001f,
            "navigation publishes camera matrices in the same frame");
        const auto beforeOrbit = view.EditorCamera.GetPosition();
        io.AddKeyEvent(ImGuiMod_Alt, true); frame(cx + 100, cy + 100, true); frame(cx + 125, cy + 110, true);
        io.AddKeyEvent(ImGuiMod_Alt, false); frame(cx + 125, cy + 110, false);
        check(VecLength(view.EditorCamera.GetPosition() - beforeOrbit) > 0.01f && !viewport.Capturing() && !uiHistory.CanUndo(), "Alt orbit leaves scene history unchanged");
        CameraEntity preview;
        preview.Transform.transform.SetTranslation(Math::Vector3(0, 0, 8));
        preview.Camera.VerticalFovDegrees = 60; preview.Camera.NearClip = 0.2f; preview.Camera.FarClip = 222;
        const auto previewId = uiScene.AddCamera(preview).Id;
        uiScene.SetActiveCamera(previewId); renderer.UseSceneCamera(true);
        const auto sourcePose = Matrix(uiScene.FindCamera(previewId)->Transform);
        const auto sourceRevision = uiScene.Revision();
        io.AddMouseWheelEvent(0, 1); frame(cx + 150, cy + 120, false);
        check(!renderer.UsesSceneCamera() && Near(view.EditorCamera.GetFOV(), XMConvertToRadians(60)) &&
            Near(view.NearClip, 0.2f) && Near(view.FarClip, 222) && uiScene.Revision() == sourceRevision &&
            Near(Matrix(uiScene.FindCamera(previewId)->Transform)._43, sourcePose._43), "navigation takes over preview pose/lens without editing scene camera");
        // A gesture ending outside the panel must release capture and record once.
        view.EditorCamera.SetEyeAtUp(Math::Vector3(0, 0, 5), Math::Vector3(0, 0, 0), Math::Vector3(Math::kYUnitVector));
        state.SelectedEntity = uiId;
        frame(cx, cy, false); frame(cx, cy, true); frame(-50, cy, true); frame(-50, cy, false);
        check(!viewport.Capturing() && uiHistory.CanUndo() && uiHistory.Undo(uiScene) && !uiHistory.CanUndo(), "drag release outside viewport records once");
        uiHistory.Clear();
        state.SelectedEntity = uiId; const auto beforeBlocked = uiScene.FindModel(uiId)->Transform;
        frame(cx, cy, true, false, false, true); frame(cx + 40, cy + 20, true, false, false, true); frame(cx, cy, false, false, false, true);
        check(Near(uiScene.FindModel(uiId)->Transform.scale, beforeBlocked.scale) && !viewport.Capturing() && !uiHistory.CanUndo(), "busy document blocks edits");
        check(uiDrawing && !GameCore::g_ViewportCapturingMouse, "viewport image bounds gizmo emit draw data and release capture");
        ImGuizmo::Enable(false); ImGui::DestroyContext(context); ImGui::SetCurrentContext(originalContext); ImGuizmo::SetImGuiContext(originalContext);
        GameCore::g_ViewportHovered = oldHovered; GameCore::g_ViewportCapturingMouse = oldCaptured;
        return result;
    }
}
