#pragma once

#include <filesystem>
#include <string>
#include <optional>
#include <vector>

#include "SceneGraph.h"
#include "EditorCommandHistory.h"
#include "ModelAssetManager.h"

class PbrRenderer;

namespace AtomEditor
{
    struct UiState
    {
        EntityId SelectedEntity = InvalidEntityId;
        std::filesystem::path ContentRoot;
        std::filesystem::path CurrentFolder;
        std::filesystem::path SelectedAsset;
        std::vector<std::filesystem::path> Entries;
        std::string StatusMessage;
        bool StatusIsError = false;
        std::vector<AssetRequest> PendingImports;
    };

    void Initialize(UiState& state);
    bool EditMaterial(Scene& scene, CommandHistory& history, EntityId id, uint32_t material,
        const std::optional<MaterialParameters>& parameters);
    void CreateCamera(Scene& scene, UiState& state, CommandHistory& history, const Math::Camera& view);
    void CreateDirectionalLight(Scene& scene, UiState& state, CommandHistory& history);
    void DeleteSelectedEntity(Scene& scene, UiState& state, CommandHistory& history);
    void DrawSceneHierarchy(Scene& scene, UiState& state, CommandHistory& history, PbrRenderer& renderer);
    void DrawInspector(Scene& scene, UiState& state, CommandHistory& history, PbrRenderer& renderer);
    void DrawContentBrowser(UiState& state, ModelAssetManager& assets);
    void ProcessImports(Scene& scene, UiState& state, CommandHistory& history, ModelAssetManager& assets);
    void CancelPendingImports(UiState& state, ModelAssetManager& assets);

    bool SelectSceneFile(bool save, std::filesystem::path& file);
}
