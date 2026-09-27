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

    TransformSnapshot CaptureTransform(const Model& model)
    {
        const auto translation = model.Transform.transform.GetTranslation();
        TransformSnapshot snapshot;
        snapshot.Translation = { translation.GetX(), translation.GetY(), translation.GetZ() };
        DirectX::XMStoreFloat4(&snapshot.Rotation, static_cast<DirectX::XMVECTOR>(model.Transform.transform.GetRotation()));
        snapshot.Scale = model.Transform.scale;
        return snapshot;
    }

    void ApplyTransform(Scene& scene, EntityId id, const TransformSnapshot& snapshot)
    {
        if (Model* model = scene.FindModel(id))
        {
            model->Transform.transform.SetTranslation(Math::Vector3(snapshot.Translation.x, snapshot.Translation.y, snapshot.Translation.z));
            model->Transform.transform.SetRotation(Math::Quaternion(DirectX::XMVectorSet(
                snapshot.Rotation.x, snapshot.Rotation.y, snapshot.Rotation.z, snapshot.Rotation.w)));
            model->Transform.scale = snapshot.Scale;
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

    void DrawSceneHierarchy(Scene& scene, UiState& state, CommandHistory& history)
    {
        ImGui::Begin("Scene Hierarchy");
        for (size_t i = 0; i < scene.Models.size(); ++i)
        {
            auto& model = scene.Models[i];
            const std::string label = model.InstanceName.empty() ? "Model " + std::to_string(i + 1) : model.InstanceName;
            if (ImGui::Selectable((label + "##entity" + std::to_string(model.Id)).c_str(), state.SelectedEntity == model.Id))
                state.SelectedEntity = model.Id;
        }
        if (scene.FindModel(state.SelectedEntity) == nullptr)
            state.SelectedEntity = InvalidEntityId;
        if (state.SelectedEntity != InvalidEntityId && ImGui::Button("Delete selected"))
        {
            const EntityId removedId = state.SelectedEntity;
            const auto selected = std::find_if(scene.Models.begin(), scene.Models.end(), [removedId](const Model& model) { return model.Id == removedId; });
            const size_t selectedIndex = static_cast<size_t>(std::distance(scene.Models.begin(), selected));
            auto removedModel = std::make_shared<std::optional<Model>>(scene.ExtractModel(removedId));
            if (removedModel->has_value())
            {
                history.RecordApplied("Delete model",
                    [removedModel, selectedIndex](Scene& target)
                    {
                        if (removedModel->has_value())
                        {
                            target.InsertModel(selectedIndex, std::move(removedModel->value()));
                            removedModel->reset();
                        }
                    },
                    [removedModel, removedId](Scene& target)
                    {
                        *removedModel = target.ExtractModel(removedId);
                    });
            }
            state.SelectedEntity = scene.Models.empty() ? InvalidEntityId : scene.Models[std::min(selectedIndex, scene.Models.size() - 1)].Id;
        }
        ImGui::End();
    }

    void DrawInspector(Scene& scene, UiState& state, CommandHistory& history)
    {
        ImGui::Begin("Inspector");
        Model* selectedModel = scene.FindModel(state.SelectedEntity);
        if (selectedModel == nullptr)
        {
            ImGui::TextDisabled("Select a model in the Scene Hierarchy.");
            ImGui::End();
            return;
        }

        auto& model = *selectedModel;
        static EntityId pendingNameEntity = InvalidEntityId;
        static std::string initialName;
        static EntityId pendingTransformEntity = InvalidEntityId;
        static TransformSnapshot initialTransform;

        char name[256]{};
        strncpy_s(name, model.InstanceName.c_str(), _TRUNCATE);
        const bool nameChanged = ImGui::InputText("Name", name, sizeof(name));
        if (ImGui::IsItemActivated())
        {
            pendingNameEntity = model.Id;
            initialName = model.InstanceName;
        }
        if (nameChanged)
        {
            model.InstanceName = name;
            scene.MarkModified();
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && pendingNameEntity == model.Id)
        {
            const EntityId id = model.Id;
            const std::string finalName = model.InstanceName;
            const std::string originalName = initialName;
            history.RecordApplied("Rename model",
                [id, originalName](Scene& target) { if (Model* item = target.FindModel(id)) item->InstanceName = originalName; },
                [id, finalName](Scene& target) { if (Model* item = target.FindModel(id)) item->InstanceName = finalName; });
            pendingNameEntity = InvalidEntityId;
        }
        ImGui::SeparatorText("Transform");
        auto translation = model.Transform.transform.GetTranslation();
        float position[3] = { translation.GetX(), translation.GetY(), translation.GetZ() };
        const bool positionChanged = ImGui::DragFloat3("Position", position, 0.05f);
        if (ImGui::IsItemActivated())
        {
            pendingTransformEntity = model.Id;
            initialTransform = CaptureTransform(model);
        }
        if (positionChanged)
        {
            model.Transform.transform.SetTranslation(Vector3(position[0], position[1], position[2]));
            scene.MarkModified();
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && pendingTransformEntity == model.Id)
        {
            const EntityId id = model.Id;
            const TransformSnapshot finalTransform = CaptureTransform(model);
            const TransformSnapshot originalTransform = initialTransform;
            history.RecordApplied("Move model",
                [id, originalTransform](Scene& target) { ApplyTransform(target, id, originalTransform); },
                [id, finalTransform](Scene& target) { ApplyTransform(target, id, finalTransform); });
            pendingTransformEntity = InvalidEntityId;
        }

        auto rotation = RotationToEulerDegrees(model.Transform.transform.GetRotation());
        float euler[3] = { rotation.x, rotation.y, rotation.z };
        const bool rotationChanged = ImGui::DragFloat3("Rotation", euler, 0.5f);
        if (ImGui::IsItemActivated())
        {
            pendingTransformEntity = model.Id;
            initialTransform = CaptureTransform(model);
        }
        if (rotationChanged)
        {
            model.Transform.transform.SetRotation(Quaternion(DirectX::XMConvertToRadians(euler[0]), DirectX::XMConvertToRadians(euler[1]), DirectX::XMConvertToRadians(euler[2])));
            scene.MarkModified();
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && pendingTransformEntity == model.Id)
        {
            const EntityId id = model.Id;
            const TransformSnapshot finalTransform = CaptureTransform(model);
            const TransformSnapshot originalTransform = initialTransform;
            history.RecordApplied("Rotate model",
                [id, originalTransform](Scene& target) { ApplyTransform(target, id, originalTransform); },
                [id, finalTransform](Scene& target) { ApplyTransform(target, id, finalTransform); });
            pendingTransformEntity = InvalidEntityId;
        }

        const auto beforeScale = CaptureTransform(model);
        const bool scaleChanged = ImGui::DragFloat("Uniform Scale", &model.Transform.scale, 0.01f, 0.001f, 10000.0f, "%.3f",
            ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        if (scaleChanged) scene.MarkModified();
        if (ImGui::IsItemActivated())
        {
            pendingTransformEntity = model.Id;
            initialTransform = beforeScale;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && pendingTransformEntity == model.Id)
        {
            const EntityId id = model.Id;
            const TransformSnapshot finalTransform = CaptureTransform(model);
            const TransformSnapshot originalTransform = initialTransform;
            history.RecordApplied("Scale model",
                [id, originalTransform](Scene& target) { ApplyTransform(target, id, originalTransform); },
                [id, finalTransform](Scene& target) { ApplyTransform(target, id, finalTransform); });
            pendingTransformEntity = InvalidEntityId;
        }
        ImGui::SeparatorText("Model Asset");
        ImGui::Text("Meshes: %u", model.Asset ? static_cast<unsigned>(model.Asset->Meshes.size()) : 0u);
        ImGui::Text("Materials: %u", model.Asset ? static_cast<unsigned>(model.Asset->Materials.size()) : 0u);
        if (model.Asset)
            ImGui::TextWrapped("%s", model.Asset->SourcePath.c_str());
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
            ImGui::PushID(static_cast<int>(id));
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
