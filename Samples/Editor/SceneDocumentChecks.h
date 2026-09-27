#pragma once

#include "SceneDocument.h"
#include "EditorPanels.h"
#include "..\PBR\PbrApplication.h"

namespace AtomEditor
{
    // Exercises the real document/import/command services against an isolated
    // scene. Created only by the application's explicit smoke-test mode.
    class SceneDocumentChecks
    {
    public:
        SceneDocumentChecks(const Scene& source, ModelAssetManager& assets, const std::filesystem::path& root);
        ~SceneDocumentChecks();
        void Update(ModelAssetManager& assets);
        ApplicationValidation Result() const { return m_Result; }

    private:
        void Check(bool passed, const char* label);
        void BeginCase();
        bool SceneUnchanged() const;

        Scene m_Scene;
        CommandHistory m_History;
        UiState m_State;
        SceneDocumentLoader m_Loader;
        ApplicationValidation m_Result{ 0, 0, 30 };
        std::filesystem::path m_Files[21];
        std::filesystem::path m_BadFile;
        std::shared_ptr<const ModelAsset> m_Asset;
        std::shared_ptr<const ModelGPU> m_GPU;
        uint64_t m_Revision = 0;
        EntityId m_FirstId = 0;
        EntityId m_Selection = 0;
        size_t m_ModelCount = 0;
        size_t m_Case = 0;
        bool m_Pending = false;
        bool m_Unchanged = true;
        ModelAssetManager& m_Assets;
        AssetCacheBudget m_OriginalBudget;
    };
}
