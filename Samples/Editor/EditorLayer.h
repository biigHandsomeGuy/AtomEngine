#pragma once

#include "EditorPanels.h"
#include "..\PBR\PbrApplication.h"

namespace AtomEditor
{
    class EditorLayer final : public PbrApplicationUi
    {
    public:
        void Initialize(Scene& scene, ModelAssetManager& assets) override;
        ViewportRequest Draw(Scene& scene, PbrRenderer& renderer, const ViewportImage& image) override;
        void Shutdown() override;

    private:
        void DrawMenu(Scene& scene);

        UiState m_State;
        CommandHistory m_History;
        ModelAssetManager* m_Assets = nullptr;
    };
}
