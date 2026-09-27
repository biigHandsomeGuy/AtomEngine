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
#include <fstream>
#include <system_error>
#include <unordered_set>

#include <Imgui/imgui.h>
#include "../../Dependencies/tinygltf/json.hpp"

#include "FileSystem.h"
#include "GameCore.h"
#include "Model.h"

#pragma comment(lib, "Comdlg32.lib")

namespace
{
    using Json = nlohmann::json;

    std::string ToUtf8(const std::filesystem::path& path)
    {
        return path.u8string();
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

    std::filesystem::path AssetPathForDocument(const Model& model)
    {
        std::filesystem::path assetPath(model.Asset ? model.Asset->SourcePath : std::string{});
        if (assetPath.empty())
            return {};

        std::error_code ec;
        const auto projectRoot = std::filesystem::path(FileSystem::GetFullPath(".")).lexically_normal();
        const auto absolute = std::filesystem::absolute(assetPath, ec).lexically_normal();
        if (!ec)
        {
            auto relative = std::filesystem::relative(absolute, projectRoot, ec);
            if (!ec && !relative.empty() && *relative.begin() != "..")
                return relative;
        }
        return assetPath;
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

    bool SaveSceneToFile(const Scene& scene, const std::filesystem::path& path, std::string& error)
    {
        Json document;
        document["format"] = "AtomEngineScene";
        document["version"] = 2;
        document["nextEntityId"] = scene.GetNextEntityId();
        document["models"] = Json::array();
        for (const auto& model : scene.Models)
        {
            const auto translation = model.Transform.transform.GetTranslation();
            const auto rotation = model.Transform.transform.GetRotation();
            DirectX::XMFLOAT4 q{};
            DirectX::XMStoreFloat4(&q, static_cast<DirectX::XMVECTOR>(rotation));
            Json entry = Json::object();
            entry["id"] = model.Id;
            entry["name"] = model.InstanceName;
            entry["asset"] = ToUtf8(AssetPathForDocument(model));
            entry["translation"] = Json::array();
            entry["translation"].push_back(static_cast<float>(translation.GetX()));
            entry["translation"].push_back(static_cast<float>(translation.GetY()));
            entry["translation"].push_back(static_cast<float>(translation.GetZ()));
            entry["rotation"] = Json::array();
            entry["rotation"].push_back(q.x);
            entry["rotation"].push_back(q.y);
            entry["rotation"].push_back(q.z);
            entry["rotation"].push_back(q.w);
            entry["scale"] = model.Transform.scale;
            document["models"].push_back(std::move(entry));
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = "Could not open scene file for writing.";
            return false;
        }
        output << document.dump(2);
        if (!output)
        {
            error = "Could not write the scene file.";
            return false;
        }
        return true;
    }

    bool LoadSceneFromFile(Scene& scene, const std::filesystem::path& path, AtomEditor::UiState& state, std::string& error)
    {
        Json document;
        try
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                error = "Could not open scene file.";
                return false;
            }
            input >> document;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }

        const int version = document.value("version", 0);
        if (document.value("format", std::string{}) != "AtomEngineScene" || (version != 1 && version != 2) || !document.contains("models") || !document["models"].is_array())
        {
            error = "Unsupported or malformed AtomEngine scene document.";
            return false;
        }

        std::vector<Model> loadedModels;
        std::unordered_set<EntityId> loadedEntityIds;
        for (const auto& item : document["models"])
        {
            try
            {
                const auto assetString = item.at("asset").get<std::string>();
                std::filesystem::path assetPath = std::filesystem::u8path(assetString);
                if (assetPath.is_relative())
                    assetPath = std::filesystem::path(FileSystem::GetFullPath(assetString));

                std::string loadError;
                auto loaded = ModelUtility::LoadGltfAsset(ToUtf8(assetPath), &loadError);
                if (!loaded.Asset)
                {
                    error = "Failed to load " + assetString + ": " + loadError;
                    return false;
                }
                Model model = ModelUtility::CreateModel(std::move(loaded));
                if (version >= 2)
                {
                    model.Id = item.at("id").get<EntityId>();
                    if (model.Id == InvalidEntityId || !loadedEntityIds.insert(model.Id).second)
                    {
                        error = "Scene contains an invalid or duplicate entity ID.";
                        return false;
                    }
                }
                model.InstanceName = item.value("name", model.InstanceName);
                const auto& t = item.at("translation");
                const auto& r = item.at("rotation");
                model.Transform.transform.SetTranslation(Vector3(t.at(0).get<float>(), t.at(1).get<float>(), t.at(2).get<float>()));
                model.Transform.transform.SetRotation(Quaternion(DirectX::XMVectorSet(r.at(0).get<float>(), r.at(1).get<float>(), r.at(2).get<float>(), r.at(3).get<float>())));
                model.Transform.scale = std::max(0.001f, item.value("scale", 1.0f));
                loadedModels.push_back(std::move(model));
            }
            catch (const std::exception& exception)
            {
                error = exception.what();
                return false;
            }
        }

        scene.ReplaceModels(std::move(loadedModels));
        if (version >= 2)
            scene.SetNextEntityId(document.value("nextEntityId", EntityId{ 1 }));
        state.SelectedEntity = scene.Models.empty() ? InvalidEntityId : scene.Models.front().Id;
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

    void DrawSceneHierarchy(Scene& scene, UiState& state)
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
            const auto selected = std::find_if(scene.Models.begin(), scene.Models.end(), [&state](const Model& model) { return model.Id == state.SelectedEntity; });
            const size_t selectedIndex = static_cast<size_t>(std::distance(scene.Models.begin(), selected));
            scene.RemoveModel(state.SelectedEntity);
            state.SelectedEntity = scene.Models.empty() ? InvalidEntityId : scene.Models[std::min(selectedIndex, scene.Models.size() - 1)].Id;
        }
        ImGui::End();
    }

    void DrawInspector(Scene& scene, UiState& state)
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
        char name[256]{};
        strncpy_s(name, model.InstanceName.c_str(), _TRUNCATE);
        if (ImGui::InputText("Name", name, sizeof(name)))
            model.InstanceName = name;
        ImGui::SeparatorText("Transform");
        auto translation = model.Transform.transform.GetTranslation();
        float position[3] = { translation.GetX(), translation.GetY(), translation.GetZ() };
        if (ImGui::DragFloat3("Position", position, 0.05f))
            model.Transform.transform.SetTranslation(Vector3(position[0], position[1], position[2]));

        auto rotation = RotationToEulerDegrees(model.Transform.transform.GetRotation());
        float euler[3] = { rotation.x, rotation.y, rotation.z };
        if (ImGui::DragFloat3("Rotation", euler, 0.5f))
            model.Transform.transform.SetRotation(Quaternion(DirectX::XMConvertToRadians(euler[0]), DirectX::XMConvertToRadians(euler[1]), DirectX::XMConvertToRadians(euler[2])));
        ImGui::DragFloat("Uniform Scale", &model.Transform.scale, 0.01f, 0.001f, 10000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SeparatorText("Model Asset");
        ImGui::Text("Meshes: %u", model.Asset ? static_cast<unsigned>(model.Asset->Meshes.size()) : 0u);
        ImGui::Text("Materials: %u", model.Asset ? static_cast<unsigned>(model.Asset->Materials.size()) : 0u);
        if (model.Asset)
            ImGui::TextWrapped("%s", model.Asset->SourcePath.c_str());
        ImGui::End();
    }

    void DrawContentBrowser(Scene& scene, UiState& state)
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
            std::string error;
            auto loaded = ModelUtility::LoadGltfAsset(ToUtf8(state.SelectedAsset), &error);
            if (loaded.Asset)
            {
                auto& importedModel = scene.AddModel(ModelUtility::CreateModel(std::move(loaded)));
                state.SelectedEntity = importedModel.Id;
                SetStatus(state, "Imported " + ToUtf8(state.SelectedAsset.filename()));
            }
            else
                SetStatus(state, "Import failed: " + error, true);
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

    bool OpenSceneDialog(Scene& scene, UiState& state)
    {
        std::filesystem::path path;
        if (!ShowSceneDialog(false, path))
            return false;
        std::string error;
        if (!LoadSceneFromFile(scene, path, state, error))
        {
            SetStatus(state, "Open failed: " + error, true);
            return false;
        }
        SetStatus(state, "Opened " + ToUtf8(path.filename()));
        return true;
    }

    bool SaveSceneDialog(const Scene& scene, UiState& state)
    {
        std::filesystem::path path;
        if (!ShowSceneDialog(true, path))
            return false;
        std::string error;
        if (!SaveSceneToFile(scene, path, error))
        {
            SetStatus(state, "Save failed: " + error, true);
            return false;
        }
        SetStatus(state, "Saved " + ToUtf8(path.filename()));
        return true;
    }
}
