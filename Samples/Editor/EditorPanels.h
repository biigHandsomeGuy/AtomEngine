#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "SceneGraph.h"
#include "EditorCommandHistory.h"
#include "ModelAssetManager.h"

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
    void DrawSceneHierarchy(Scene& scene, UiState& state, CommandHistory& history);
    void DrawInspector(Scene& scene, UiState& state, CommandHistory& history);
    void DrawContentBrowser(UiState& state, ModelAssetManager& assets);
    void ProcessImports(Scene& scene, UiState& state, CommandHistory& history, ModelAssetManager& assets);
    void CancelPendingImports(UiState& state, ModelAssetManager& assets);

    bool SelectSceneFile(bool save, std::filesystem::path& file);
}
