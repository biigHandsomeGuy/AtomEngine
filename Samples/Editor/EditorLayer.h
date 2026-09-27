#pragma once

#include "EditorPanels.h"
#include "..\PBR\PbrApplication.h"
#include "SceneDocument.h"
#include "SceneDocumentChecks.h"

namespace AtomEditor
{
    class EditorLayer final : public PbrApplicationUi
    {
    public:
        void Initialize(Scene& scene, ModelAssetManager& assets) override;
        void StartValidation(const Scene& scene, ModelAssetManager& assets) override;
        ApplicationValidation Validation() const override;
        ViewportRequest Draw(Scene& scene, PbrRenderer& renderer, const ViewportImage& image) override;
        void Shutdown() override;

    private:
        void DrawMenu(Scene& scene);
        void OpenDocument();
        void SaveDocument(const Scene& scene, bool saveAs);
        void UpdateDocument(Scene& scene, PbrRenderer& renderer);

        UiState m_State;
        CommandHistory m_History;
        ModelAssetManager* m_Assets = nullptr;
        std::unique_ptr<SceneDocumentLoader> m_DocumentLoader;
        std::unique_ptr<SceneDocumentChecks> m_Checks;
        std::filesystem::path m_ProjectRoot;
        std::filesystem::path m_DocumentPath;
        SceneLoadState m_LastLoadState = SceneLoadState::Idle;
    };
}
