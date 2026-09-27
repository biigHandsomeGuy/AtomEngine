#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef ERROR
#undef ERROR
#endif
#include "pch.h"
#include "EditorPanels.h"

#include <algorithm>
#include <cctype>
#include <commdlg.h>
#include <cmath>
#include <cstring>

#include <memory>
#include <optional>
#include <system_error>

#include <Imgui/imgui.h>

#include "FileSystem.h"
#include "GameCore.h"
#include "Model.h"
#include "PathUtils.h"
#include "PbrRenderer.h"

#pragma comment(lib, "Comdlg32.lib")

namespace
{

    std::string ToUtf8(const std::filesystem::path& path)
    {
        return PathToUtf8(path);
    }

    DirectX::XMFLOAT3 RotationToEulerDegrees(const Math::Quaternion& rotation)
    {
        DirectX::XMFLOAT4 q{};
        DirectX::XMStoreFloat4(&q, static_cast<DirectX::XMVECTOR>(rotation));
        const float sinPitch = 2.0f * (q.w * q.x - q.z * q.y);
        const float pitch = std::abs(sinPitch) >= 1.0f
            ? std::copysign(DirectX::XM_PIDIV2, sinPitch)
            : std::asin(sinPitch);
        const float yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        const float roll = std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.x * q.x + q.z * q.z));
        constexpr float toDegrees = 180.0f / DirectX::XM_PI;
        return { pitch * toDegrees, yaw * toDegrees, roll * toDegrees };
    }

    struct TransformSnapshot
    {
        DirectX::XMFLOAT3 Translation{};
        DirectX::XMFLOAT4 Rotation{};
        float Scale = 1.0f;
    };

    TransformSnapshot CaptureTransform(const TransformComponent& transform)
    {
        const auto translation = transform.transform.GetTranslation();
        TransformSnapshot snapshot;
        snapshot.Translation = { translation.GetX(), translation.GetY(), translation.GetZ() };
        DirectX::XMStoreFloat4(&snapshot.Rotation, static_cast<DirectX::XMVECTOR>(transform.transform.GetRotation()));
        snapshot.Scale = transform.scale;
        return snapshot;
    }

    void ApplyTransform(Scene& scene, EntityId id, const TransformSnapshot& snapshot)
    {
        if (auto* transform = scene.EntityTransform(id))
        {
            transform->transform.SetTranslation(Math::Vector3(snapshot.Translation.x, snapshot.Translation.y, snapshot.Translation.z));
            transform->transform.SetRotation(Math::Quaternion(DirectX::XMVectorSet(
                snapshot.Rotation.x, snapshot.Rotation.y, snapshot.Rotation.z, snapshot.Rotation.w)));
            transform->scale = snapshot.Scale;
        }
    }

    void SetStatus(AtomEditor::UiState& state, std::string message, bool isError = false)
    {
        state.StatusMessage = std::move(message);
        state.StatusIsError = isError;
    }

    bool IsModelFile(const std::filesystem::path& path)
    {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".gltf" || extension == ".glb";
    }

    void RefreshEntries(AtomEditor::UiState& state)
    {
        state.Entries.clear();
        std::error_code ec;
        if (!std::filesystem::exists(state.CurrentFolder, ec))
        {
            SetStatus(state, "Content folder does not exist: " + ToUtf8(state.CurrentFolder), true);
            return;
        }
        for (std::filesystem::directory_iterator it(state.CurrentFolder, ec), end; !ec && it != end; it.increment(ec))
        {
            const auto& entry = *it;
            if (entry.is_directory(ec) || IsModelFile(entry.path()))
                state.Entries.push_back(entry.path());
        }
        std::sort(state.Entries.begin(), state.Entries.end(), [](const auto& a, const auto& b)
        {
            const bool aDir = std::filesystem::is_directory(a);
            const bool bDir = std::filesystem::is_directory(b);
            return aDir != bDir ? aDir : a.filename().wstring() < b.filename().wstring();
        });
        if (ec)
            SetStatus(state, "Could not enumerate content folder: " + ec.message(), true);
    }

    bool ShowSceneDialog(bool save, std::filesystem::path& selectedPath)
    {
        wchar_t fileName[MAX_PATH]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = GameCore::g_hWnd;
        dialog.lpstrFilter = L"Atom Scene (*.atomscene)\0*.atomscene\0JSON (*.json)\0*.json\0All files (*.*)\0*.*\0\0";
        dialog.lpstrFile = fileName;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrDefExt = L"atomscene";
        dialog.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        const BOOL result = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
        if (!result)
            return false;
        selectedPath = fileName;
        return true;
    }

}

namespace AtomEditor
{
    void Initialize(UiState& state)
    {
        state.ContentRoot = std::filesystem::path(FileSystem::GetFullPath("Assets/Models"));
        state.CurrentFolder = state.ContentRoot;
        RefreshEntries(state);
    }


    namespace
    {
        void RecordCreation(Scene& scene, UiState& state, CommandHistory& history, EntityId id,
            const char* label, EntityId oldCamera, EntityId oldLight)
        {
            const auto index = scene.EntityIndex(id);
            const auto newCamera = scene.ActiveCameraId();
            const auto newLight = scene.ActiveLightId();
            auto payload = std::make_shared<std::optional<Scene::EntityData>>();
            history.RecordApplied(label,
                [payload, id, oldCamera, oldLight](Scene& target)
                {
                    *payload = target.ExtractEntity(id);
                    target.SetActiveCamera(oldCamera); target.SetActiveLight(oldLight);
                },
                [payload, index, newCamera, newLight](Scene& target)
                {
                    if (*payload)
                    {
                        target.InsertEntity(index, std::move(payload->value())); payload->reset();
                        target.SetActiveCamera(newCamera); target.SetActiveLight(newLight);
                    }
                });
            state.SelectedEntity = id;
        }
    }

    void CreateCamera(Scene& scene, UiState& state, CommandHistory& history, const Math::Camera& view)
    {
        const auto oldCamera = scene.ActiveCameraId(), oldLight = scene.ActiveLightId();
        CameraEntity camera;
        camera.Transform.transform.SetTranslation(view.GetPosition());
        camera.Transform.transform.SetRotation(view.GetRotation());
        camera.Camera = { DirectX::XMConvertToDegrees(view.GetFOV()), view.GetNearClip(), view.GetFarClip() };
        const auto id = scene.AddCamera(std::move(camera)).Id;
        if (!oldCamera) scene.SetActiveCamera(id);
        RecordCreation(scene, state, history, id, "Create camera", oldCamera, oldLight);
    }

    void CreateDirectionalLight(Scene& scene, UiState& state, CommandHistory& history)
    {
        const auto oldCamera = scene.ActiveCameraId(), oldLight = scene.ActiveLightId();
        const auto id = scene.AddLight(Scene::DefaultSun()).Id;
        if (!oldLight) scene.SetActiveLight(id);
        RecordCreation(scene, state, history, id, "Create directional light", oldCamera, oldLight);
    }

    void DeleteSelectedEntity(Scene& scene, UiState& state, CommandHistory& history)
    {
        const auto id = state.SelectedEntity;
        if (!scene.Contains(id)) return;
        const auto index = scene.EntityIndex(id);
        const auto camera = scene.ActiveCameraId(), light = scene.ActiveLightId();
        auto payload = std::make_shared<std::optional<Scene::EntityData>>(scene.ExtractEntity(id));
        history.RecordApplied("Delete entity",
            [payload, index, camera, light](Scene& target)
            {
                if (*payload)
                {
                    target.InsertEntity(index, std::move(payload->value())); payload->reset();
                    target.SetActiveCamera(camera); target.SetActiveLight(light);
                }
            },
            [payload, id](Scene& target) { *payload = target.ExtractEntity(id); });
        state.SelectedEntity = scene.FirstEntityId();
    }

    void DrawSceneHierarchy(Scene& scene, UiState& state, CommandHistory& history, PbrRenderer& renderer)
    {
        ImGui::Begin("Scene Hierarchy");
        if (ImGui::Button("Add camera")) CreateCamera(scene, state, history, renderer.EditorCamera());
        ImGui::SameLine();
        if (ImGui::Button("Add directional light")) CreateDirectionalLight(scene, state, history);
        auto drawList = [&](const auto& list, const char* type)
        {
            for (const auto& item : list)
            {
                const auto label = std::string(type) + " " + (item.InstanceName.empty() ? std::to_string(item.Id) : item.InstanceName);
                if (ImGui::Selectable((label + "##entity" + std::to_string(item.Id)).c_str(), state.SelectedEntity == item.Id))
                    state.SelectedEntity = item.Id;
            }
        };
        drawList(scene.Models, "[Model]"); drawList(scene.Cameras, "[Camera]"); drawList(scene.Lights, "[Light]");
        if (!scene.Contains(state.SelectedEntity)) state.SelectedEntity = InvalidEntityId;
        if (state.SelectedEntity && ImGui::Button("Delete selected")) DeleteSelectedEntity(scene, state, history);
        ImGui::End();
    }

    template<class T, class Apply> void TrackComponentEdit(Scene& scene, CommandHistory& history, EntityId id,
        const T& before, const T& after, bool changed, const char* label, Apply apply, bool instant = false)
    {
        static EntityId pending = InvalidEntityId;
        static T initial;
        if (ImGui::IsItemActivated()) { pending = id; initial = before; }
        if (changed) scene.MarkModified();
        if ((instant && changed) || (ImGui::IsItemDeactivatedAfterEdit() && pending == id))
        {
            const auto first = instant ? before : initial;
            const auto last = after;
            history.RecordApplied(label,
                [id, first, apply](Scene& target) { apply(target, id, first); },
                [id, last, apply](Scene& target) { apply(target, id, last); });
            pending = InvalidEntityId;
        }
    }

    namespace
    {
        using MaterialEdit = std::optional<MaterialParameters>;
        MaterialEdit OverrideOf(const Model& model, uint32_t index)
        {
            const auto found = model.MaterialOverrides.find(index);
            return found == model.MaterialOverrides.end() ? MaterialEdit{} : MaterialEdit{found->second};
        }
        void ApplyMaterial(Scene& scene, EntityId id, uint32_t index, const MaterialEdit& values)
        {
            if (auto* model = scene.FindModel(id))
            {
                if (values) model->SetMaterialOverride(index, *values);
                else model->ResetMaterialOverride(index);
            }
        }
        void RecordMaterial(Scene& scene, CommandHistory& history, EntityId id, uint32_t index,
            const MaterialEdit& before, const MaterialEdit& after)
        {
            history.RecordApplied("Edit material",
                [id, index, before](Scene& target) { ApplyMaterial(target, id, index, before); },
                [id, index, after](Scene& target) { ApplyMaterial(target, id, index, after); });
            scene.MarkModified();
        }
        void TrackMaterial(Scene& scene, CommandHistory& history, Model& model, uint32_t index,
            const MaterialEdit& before, const MaterialParameters& after, bool changed, bool instant = false)
        {
            static EntityId pending = InvalidEntityId;
            static uint32_t pendingIndex = 0;
            static MaterialEdit initial;
            if (ImGui::IsItemActivated()) { pending = model.Id; pendingIndex = index; initial = before; }
            if (changed && model.SetMaterialOverride(index, after)) scene.MarkModified();
            if ((instant && changed) || (ImGui::IsItemDeactivatedAfterEdit() && pending == model.Id && pendingIndex == index))
            {
                RecordMaterial(scene, history, model.Id, index, instant ? before : initial, OverrideOf(model, index));
                pending = InvalidEntityId;
            }
        }
        void DrawMaterials(Scene& scene, CommandHistory& history, Model& model)
        {
            if (!model.Asset || !model.GPU) return;
            ImGui::SeparatorText("Materials");
            for (uint32_t index = 0; index < model.Asset->Materials.size(); ++index)
            {
                const bool used = std::any_of(model.Asset->Meshes.begin(), model.Asset->Meshes.end(), [&](const MeshAsset& mesh)
                {
                    return std::any_of(mesh.SubMeshes.begin(), mesh.SubMeshes.end(), [&](const SubMesh& sub)
                    {
                        const auto resolved = sub.MaterialIndex < model.Asset->Materials.size() ? sub.MaterialIndex : model.Asset->Materials.size() - 1;
                        return resolved == index;
                    });
                });
                if (!used) continue;
                ImGui::PushID(static_cast<int>(index));
                const auto& name = model.Asset->Materials[index].Name;
                const auto label = name.empty() ? "Material " + std::to_string(index) : name;
                if (ImGui::TreeNode("material", "%s%s", label.c_str(), model.MaterialOverrides.count(index) ? " *" : ""))
                {
                    auto values = model.MaterialValues(index);
                    auto before = OverrideOf(model, index);
                    bool changed = ImGui::ColorEdit3("Base color (linear)", &values.BaseColorFactor.x, ImGuiColorEditFlags_Float);
                    TrackMaterial(scene, history, model, index, before, values, changed);
                    before = OverrideOf(model, index);
                    changed = ImGui::SliderFloat("Metallic", &values.MetallicFactor, 0, 1);
                    TrackMaterial(scene, history, model, index, before, values, changed);
                    before = OverrideOf(model, index);
                    changed = ImGui::SliderFloat("Roughness", &values.RoughnessFactor, 0, 1);
                    TrackMaterial(scene, history, model, index, before, values, changed);
                    before = OverrideOf(model, index);
                    changed = ImGui::ColorEdit3("Emission (linear HDR)", &values.EmissiveFactor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                    TrackMaterial(scene, history, model, index, before, values, changed);
                    before = OverrideOf(model, index);
                    changed = ImGui::DragFloat("Normal scale", &values.NormalScale, 0.01f, 0, 10, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    TrackMaterial(scene, history, model, index, before, values, changed);
                    const char* textures[] = { "Base color texture", "Normal texture", "Metallic/roughness texture", "Emissive texture" };
                    for (uint32_t bit = 0; bit < 4; ++bit)
                    {
                        const uint32_t mask = 1u << bit;
                        const bool available = (model.GPU->Materials[index].TextureFlags & mask) != 0;
                        ImGui::BeginDisabled(!available);
                        bool enabled = (values.TextureFlags & mask) != 0;
                        before = OverrideOf(model, index);
                        changed = ImGui::Checkbox(textures[bit], &enabled);
                        if (changed) values.TextureFlags = enabled ? values.TextureFlags | mask : values.TextureFlags & ~mask;
                        TrackMaterial(scene, history, model, index, before, values, changed, true);
                        ImGui::EndDisabled();
                    }
                    if (model.MaterialOverrides.count(index) && ImGui::Button("Reset to imported material"))
                        EditMaterial(scene, history, model.Id, index, {});
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
    }

    bool EditMaterial(Scene& scene, CommandHistory& history, EntityId id, uint32_t index, const MaterialEdit& parameters)
    {
        auto* model = scene.FindModel(id);
        if (!model || !model->Asset || index >= model->Asset->Materials.size() ||
            (parameters && !ValidMaterialParameters(*parameters))) return false;
        const auto before = OverrideOf(*model, index);
        ApplyMaterial(scene, id, index, parameters);
        RecordMaterial(scene, history, id, index, before, parameters);
        return true;
    }

    void DrawInspector(Scene& scene, UiState& state, CommandHistory& history, PbrRenderer& renderer)
    {
        ImGui::Begin("Inspector");
        const auto id = state.SelectedEntity;
        auto* entityName = scene.EntityName(id);
        auto* entityTransform = scene.EntityTransform(id);
        if (!entityName || !entityTransform)
        {
            ImGui::TextDisabled("Select an entity in the Scene Hierarchy.");
            ImGui::End();
            return;
        }
        auto& transform = *entityTransform;
        static EntityId pendingNameEntity = InvalidEntityId;
        static std::string initialName;
        static EntityId pendingTransformEntity = InvalidEntityId;
        static TransformSnapshot initialTransform;

        char name[256]{};
        strncpy_s(name, entityName->c_str(), _TRUNCATE);
        const bool nameChanged = ImGui::InputText("Name", name, sizeof(name));
        if (ImGui::IsItemActivated())
        {
            pendingNameEntity = state.SelectedEntity;
            initialName = *entityName;
        }
        if (nameChanged)
        {
            *entityName = name;
            scene.MarkModified();
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && pendingNameEntity == state.SelectedEntity)
        {
            const EntityId id = state.SelectedEntity;
            const std::string finalName = *entityName;
            const std::string originalName = initialName;
            history.RecordApplied("Rename entity",
                [id, originalName](Scene& target) { if (auto* name = target.EntityName(id)) *name = originalName; },
                [id, finalName](Scene& target) { if (auto* name = target.EntityName(id)) *name = finalName; });
            pendingNameEntity = InvalidEntityId;
        }
        ImGui::SeparatorText("Transform");
        auto translation = transform.transform.GetTranslation();
        float position[3] = { translation.GetX(), translation.GetY(), translation.GetZ() };
        const bool positionChanged = ImGui::DragFloat3("Position", position, 0.05f);
        if (ImGui::IsItemActivated())
        {
            pendingTransformEntity = state.SelectedEntity;
            initialTransform = CaptureTransform(transform);
        }
        if (positionChanged)
        {
            transform.transform.SetTranslation(Vector3(position[0], position[1], position[2]));
            scene.MarkModified();
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && pendingTransformEntity == state.SelectedEntity)
        {
            const EntityId id = state.SelectedEntity;
            const TransformSnapshot finalTransform = CaptureTransform(transform);
            const TransformSnapshot originalTransform = initialTransform;
            history.RecordApplied("Move entity",
                [id, originalTransform](Scene& target) { ApplyTransform(target, id, originalTransform); },
                [id, finalTransform](Scene& target) { ApplyTransform(target, id, finalTransform); });
            pendingTransformEntity = InvalidEntityId;
        }

        auto rotation = RotationToEulerDegrees(transform.transform.GetRotation());
        float euler[3] = { rotation.x, rotation.y, rotation.z };
        const bool rotationChanged = ImGui::DragFloat3("Rotation", euler, 0.5f);
        if (ImGui::IsItemActivated())
        {
            pendingTransformEntity = state.SelectedEntity;
            initialTransform = CaptureTransform(transform);
        }
        if (rotationChanged)
        {
            transform.transform.SetRotation(Quaternion(DirectX::XMConvertToRadians(euler[0]), DirectX::XMConvertToRadians(euler[1]), DirectX::XMConvertToRadians(euler[2])));
            scene.MarkModified();
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && pendingTransformEntity == state.SelectedEntity)
        {
            const EntityId id = state.SelectedEntity;
            const TransformSnapshot finalTransform = CaptureTransform(transform);
            const TransformSnapshot originalTransform = initialTransform;
            history.RecordApplied("Rotate entity",
                [id, originalTransform](Scene& target) { ApplyTransform(target, id, originalTransform); },
                [id, finalTransform](Scene& target) { ApplyTransform(target, id, finalTransform); });
            pendingTransformEntity = InvalidEntityId;
        }

        if (scene.FindModel(id))
        {
            const auto beforeScale = CaptureTransform(transform);
            const bool scaleChanged = ImGui::DragFloat("Uniform Scale", &transform.scale, 0.01f, 0.001f, 10000.0f, "%.3f",
                ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            if (scaleChanged) scene.MarkModified();
            if (ImGui::IsItemActivated())
            {
                pendingTransformEntity = state.SelectedEntity;
                initialTransform = beforeScale;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && pendingTransformEntity == state.SelectedEntity)
            {
                const EntityId id = state.SelectedEntity;
                const TransformSnapshot finalTransform = CaptureTransform(transform);
                const TransformSnapshot originalTransform = initialTransform;
                history.RecordApplied("Scale model",
                    [id, originalTransform](Scene& target) { ApplyTransform(target, id, originalTransform); },
                    [id, finalTransform](Scene& target) { ApplyTransform(target, id, finalTransform); });
                pendingTransformEntity = InvalidEntityId;
            }
        }
        if (auto* model = scene.FindModel(id))
        {
            DrawMaterials(scene, history, *model);
            ImGui::SeparatorText("Model Asset");
            ImGui::Text("Meshes: %u", model->Asset ? static_cast<unsigned>(model->Asset->Meshes.size()) : 0u);
            ImGui::Text("Materials: %u", model->Asset ? static_cast<unsigned>(model->Asset->Materials.size()) : 0u);
            if (model->Asset) ImGui::TextWrapped("%s", model->Asset->SourcePath.c_str());
        }
        if (auto* camera = scene.FindCamera(id))
        {
            ImGui::SeparatorText("Perspective Camera");
            const auto apply = [](Scene& target, EntityId entity, const CameraComponent& value)
                { if (auto* item = target.FindCamera(entity)) item->Camera = value; };
            auto before = camera->Camera;
            bool changed = ImGui::DragFloat("Vertical FOV (degrees)", &camera->Camera.VerticalFovDegrees, 0.1f, 1, 179, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            TrackComponentEdit(scene, history, id, before, camera->Camera, changed, "Camera field of view", apply);
            before = camera->Camera;
            changed = ImGui::DragFloat("Near clip", &camera->Camera.NearClip, 0.01f, 0.001f,
                std::min(100000.0f, camera->Camera.FarClip - 0.001f), "%.3f", ImGuiSliderFlags_AlwaysClamp);
            TrackComponentEdit(scene, history, id, before, camera->Camera, changed, "Camera near clip", apply);
            before = camera->Camera;
            changed = ImGui::DragFloat("Far clip", &camera->Camera.FarClip, 1.0f, camera->Camera.NearClip + 0.01f, 10000000, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            TrackComponentEdit(scene, history, id, before, camera->Camera, changed, "Camera far clip", apply);
            if (ImGui::Button("Copy editor view"))
            {
                const auto original = *camera;
                camera->Transform.transform.SetTranslation(renderer.EditorCamera().GetPosition());
                camera->Transform.transform.SetRotation(renderer.EditorCamera().GetRotation());
                camera->Camera = { DirectX::XMConvertToDegrees(renderer.EditorCamera().GetFOV()),
                    renderer.EditorCamera().GetNearClip(), renderer.EditorCamera().GetFarClip() };
                const auto final = *camera;
                history.RecordApplied("Copy editor view to camera",
                    [id, original](Scene& target) { if (auto* item = target.FindCamera(id)) *item = original; },
                    [id, final](Scene& target) { if (auto* item = target.FindCamera(id)) *item = final; });
                scene.MarkModified();
            }
            if (scene.ActiveCameraId() == id) ImGui::TextDisabled("Active scene camera");
            else if (ImGui::Button("Set as scene camera"))
            {
                const auto old = scene.ActiveCameraId(); scene.SetActiveCamera(id);
                history.RecordApplied("Set scene camera", [old](Scene& target) { target.SetActiveCamera(old); },
                    [id](Scene& target) { target.SetActiveCamera(id); });
            }
        }
        if (auto* light = scene.FindLight(id))
        {
            ImGui::SeparatorText("Directional Light");
            ImGui::TextWrapped("Rotation controls the emitted ray direction (local -Z). Color uses linear RGB.");
            const auto apply = [](Scene& target, EntityId entity, const DirectionalLightComponent& value)
                { if (auto* item = target.FindLight(entity)) item->Light = value; };
            auto before = light->Light;
            bool changed = ImGui::ColorEdit3("Color", &light->Light.Color.x);
            TrackComponentEdit(scene, history, id, before, light->Light, changed, "Light color", apply);
            before = light->Light;
            changed = ImGui::DragFloat("Intensity", &light->Light.Intensity, 0.05f, 0, 10000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            TrackComponentEdit(scene, history, id, before, light->Light, changed, "Light intensity", apply);
            before = light->Light;
            changed = ImGui::Checkbox("Enabled", &light->Light.Enabled);
            TrackComponentEdit(scene, history, id, before, light->Light, changed, "Light enabled", apply, true);
            before = light->Light;
            changed = ImGui::Checkbox("Cast shadows", &light->Light.CastsShadows);
            TrackComponentEdit(scene, history, id, before, light->Light, changed, "Light shadows", apply, true);
            before = light->Light;
            changed = ImGui::DragFloat3("Shadow center", &light->Light.ShadowCenter.x, 0.1f);
            TrackComponentEdit(scene, history, id, before, light->Light, changed, "Shadow center", apply);
            before = light->Light;
            changed = ImGui::DragFloat3("Shadow bounds", &light->Light.ShadowBounds.x, 1, 0.001f, 10000000, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            TrackComponentEdit(scene, history, id, before, light->Light, changed, "Shadow bounds", apply);
            if (scene.ActiveLightId() == id) ImGui::TextDisabled("Active scene light");
            else if (ImGui::Button("Use for direct lighting"))
            {
                const auto old = scene.ActiveLightId(); scene.SetActiveLight(id);
                history.RecordApplied("Set scene light", [old](Scene& target) { target.SetActiveLight(old); },
                    [id](Scene& target) { target.SetActiveLight(id); });
            }
        }
        ImGui::End();
    }

    void DrawContentBrowser(UiState& state, ModelAssetManager& assets)
    {
        ImGui::Begin("Content Browser");
        if (ImGui::Button("Up") && state.CurrentFolder != state.ContentRoot && state.CurrentFolder.has_parent_path())
        {
            state.CurrentFolder = state.CurrentFolder.parent_path();
            RefreshEntries(state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            RefreshEntries(state);
        auto cache = assets.CacheStats();
        constexpr uint64_t mib = 1024 * 1024;
        int cpuBudget = static_cast<int>(std::min(cache.Budget.CpuBytes / mib, uint64_t(65536)));
        int gpuBudget = static_cast<int>(std::min(cache.Budget.GpuBytes / mib, uint64_t(65536)));
        const bool cpuChanged = ImGui::InputInt("CPU cache budget (MiB)", &cpuBudget);
        const bool gpuChanged = ImGui::InputInt("GPU cache budget (MiB)", &gpuBudget);
        if (cpuChanged || gpuChanged)
        {
            assets.SetBudget({ static_cast<uint64_t>(std::clamp(cpuBudget, 0, 65536)) * mib,
                static_cast<uint64_t>(std::clamp(gpuBudget, 0, 65536)) * mib });
            cache = assets.CacheStats();
        }
        ImGui::Text("Cache: CPU %.1f MiB / GPU %.1f MiB",
            double(cache.CpuBytes) / mib, double(cache.GpuBytes) / mib);
        ImGui::Text("%zu entries, %zu protected, %llu evicted", cache.Entries,
            cache.ProtectedEntries, static_cast<unsigned long long>(cache.Evictions));
        if (cache.OverBudget())
            ImGui::TextWrapped("Budget exceeded: active loads, scenes, undo history or unfinished frames still use these assets.");
        ImGui::TextWrapped("%s", ToUtf8(state.CurrentFolder).c_str());
        ImGui::BeginChild("ContentEntries", ImVec2(0, 180), true);
        for (const auto& entry : state.Entries)
        {
            const bool directory = std::filesystem::is_directory(entry);
            const std::string label = (directory ? "[Folder] " : "[Model] ") + ToUtf8(entry.filename());
            const bool selected = state.SelectedAsset == entry;
            if (ImGui::Selectable((label + "##" + ToUtf8(entry)).c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
            {
                state.SelectedAsset = entry;
                if (directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    state.CurrentFolder = entry;
                    RefreshEntries(state);
                }
            }
        }
        ImGui::EndChild();
        if (!state.SelectedAsset.empty() && IsModelFile(state.SelectedAsset) && ImGui::Button("Import selected model"))
        {
            const auto id = assets.Request(ToUtf8(state.SelectedAsset));
            if (std::find(state.PendingImports.begin(), state.PendingImports.end(), id) == state.PendingImports.end())
                state.PendingImports.push_back(id);
            SetStatus(state, "Import queued: " + ToUtf8(state.SelectedAsset.filename()));
        }
        for (const auto id : state.PendingImports)
        {
            const auto job = assets.Snapshot(id);
            ImGui::PushID(static_cast<int>(id.Id()));
            ImGui::TextWrapped("%s: %s", ToUtf8(std::filesystem::u8path(job.SourcePath).filename()).c_str(), ModelAssetManager::StateName(job.State));
            if (ImGui::Button(job.CancelRequested ? "Cancelling..." : "Cancel import"))
                assets.Cancel(id);
            ImGui::PopID();
        }
        if (!state.StatusMessage.empty())
        {
            if (state.StatusIsError)
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%s", state.StatusMessage.c_str());
            else
                ImGui::TextWrapped("%s", state.StatusMessage.c_str());
        }
        ImGui::End();
    }

    void ProcessImports(Scene& scene, UiState& state, CommandHistory& history, ModelAssetManager& assets)
    {
        for (auto iterator = state.PendingImports.begin(); iterator != state.PendingImports.end();)
        {
            auto job = assets.Snapshot(*iterator);
            if (job.State == AssetLoadState::Ready)
            {
                const size_t index = scene.Models.size();
                auto payload = std::make_shared<std::optional<Model>>();
                Model& model = scene.AddModel(ModelUtility::CreateModel(std::move(job.Data)));
                const EntityId id = model.Id;
                history.RecordApplied("Import model",
                    [payload, id](Scene& target) { *payload = target.ExtractModel(id); },
                    [payload, index](Scene& target)
                    {
                        if (payload->has_value())
                        {
                            target.InsertModel(index, std::move(payload->value()));
                            payload->reset();
                        }
                    });
                state.SelectedEntity = id;
                SetStatus(state, "Imported " + ToUtf8(std::filesystem::u8path(job.SourcePath).filename()) +
                    (job.Warnings.empty() ? "" : "\n" + job.Warnings));
            }
            else if (job.State == AssetLoadState::Failed)
                SetStatus(state, "Import failed: " + job.Error, true);
            else if (job.State == AssetLoadState::Cancelled)
                SetStatus(state, "Import cancelled.");
            else { ++iterator; continue; }
            iterator = state.PendingImports.erase(iterator);
        }
    }

    void CancelPendingImports(UiState& state, ModelAssetManager& assets)
    {
        for (const auto id : state.PendingImports) assets.Cancel(id);
        state.PendingImports.clear();
    }
    bool SelectSceneFile(bool save, std::filesystem::path& file)
    {
        return ShowSceneDialog(save, file);
    }
}
