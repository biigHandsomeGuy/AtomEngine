#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "SceneGraph.h"
#include "EditorCommandHistory.h"

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
    };

    void Initialize(UiState& state);
    void DrawSceneHierarchy(Scene& scene, UiState& state, CommandHistory& history);
    void DrawInspector(Scene& scene, UiState& state, CommandHistory& history);
    void DrawContentBrowser(Scene& scene, UiState& state, CommandHistory& history);

    bool OpenSceneDialog(Scene& scene, UiState& state, CommandHistory& history);
    bool SaveSceneDialog(const Scene& scene, UiState& state);
}
