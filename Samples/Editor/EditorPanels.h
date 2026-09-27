#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Scene.h"

namespace AtomEditor
{
    struct UiState
    {
        int SelectedModel = -1;
        std::filesystem::path ContentRoot;
        std::filesystem::path CurrentFolder;
        std::filesystem::path SelectedAsset;
        std::vector<std::filesystem::path> Entries;
        std::string StatusMessage;
        bool StatusIsError = false;
    };

    void Initialize(UiState& state);
    void DrawSceneHierarchy(Scene& scene, UiState& state);
    void DrawInspector(Scene& scene, UiState& state);
    void DrawContentBrowser(Scene& scene, UiState& state);

    bool OpenSceneDialog(Scene& scene, UiState& state);
    bool SaveSceneDialog(const Scene& scene, UiState& state);
}
