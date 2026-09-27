#include "pch.h"
#include "SceneDocumentChecks.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "PathUtils.h"
#include "../../Dependencies/tinygltf/json.hpp"

namespace AtomEditor
{
    namespace
    {
        using Json = nlohmann::json;
        std::string Bytes(const std::filesystem::path& file)
        {
            std::ifstream input(file, std::ios::binary);
            return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        }
        void Write(const std::filesystem::path& file, const Json& document)
        {
            std::ofstream output(file, std::ios::binary | std::ios::trunc);
            output << document.dump(2);
            if (!output) throw std::runtime_error("Could not write scene validation fixture.");
        }
    }

    void SceneDocumentChecks::Check(bool passed, const char* label)
    {
        ++m_Result.Completed;
        if (!passed)
        {
            ++m_Result.Errors;
            std::cerr << "EDITOR_CHECK_FAILED " << label << '\n';
        }
    }

    SceneDocumentChecks::SceneDocumentChecks(const Scene& source, ModelAssetManager& assets,
        const std::filesystem::path& root) : m_Loader(root), m_Assets(assets), m_OriginalBudget(assets.CacheStats().Budget)
    {
        try
        {
            assets.SetBudget({ 0, 0 });
            if (source.Models.empty() || !source.Models.front().Asset || !source.Models.front().GPU)
                throw std::runtime_error("Validation requires a resident startup model.");
            m_Asset = source.Models.front().Asset;
            m_GPU = source.Models.front().GPU;
            auto model = ModelUtility::CreateModel(LoadedModelData{ m_Asset, m_GPU });
            model.Id = 41;
            model.InstanceName = "Roundtrip model";
            model.Transform.scale = 2.5f;
            model.Transform.transform.SetTranslation(Math::Vector3(1, 2, 3));
            m_Scene.AddModel(std::move(model));
            const auto id = m_Scene.Models.front().Id;

            m_Scene.Models.front().Transform.scale = 3.5f;
            m_Scene.MarkModified();
            m_History.RecordApplied("Scale", [id](Scene& s) { s.FindModel(id)->Transform.scale = 2.5f; },
                [id](Scene& s) { s.FindModel(id)->Transform.scale = 3.5f; });
            const auto revision = m_Scene.Revision();
            const bool undo = m_History.Undo(m_Scene) && m_Scene.FindModel(id)->Transform.scale == 2.5f;
            const bool redo = m_History.Redo(m_Scene) && m_Scene.FindModel(id)->Transform.scale == 3.5f;
            Check(undo && redo && m_Scene.Revision() >= revision + 2, "transform undo/redo and revision");
            m_Scene.FindModel(id)->Transform.scale = 2.5f;
            m_Scene.MarkModified();

            auto removed = std::make_shared<std::optional<Model>>(m_Scene.ExtractModel(id));
            m_History.RecordApplied("Delete", [removed](Scene& s)
                { s.InsertModel(0, std::move(removed->value())); removed->reset(); },
                [removed, id](Scene& s) { *removed = s.ExtractModel(id); });
            const bool restored = m_History.Undo(m_Scene) && m_Scene.FindModel(id) && m_Scene.FindModel(id)->GPU == m_GPU;
            const bool deleted = m_History.Redo(m_Scene) && !m_Scene.FindModel(id);
            Check(restored && deleted && m_History.Undo(m_Scene), "delete undo/redo retains identity and residency");

            m_State.PendingImports.push_back(assets.Request(m_Asset->SourcePath));
            ProcessImports(m_Scene, m_State, m_History, assets);
            const auto importedId = m_State.SelectedEntity;
            const bool imported = m_State.PendingImports.empty() && m_Scene.Models.size() == 2 && importedId != id;
            const bool importUndo = m_History.Undo(m_Scene) && !m_Scene.FindModel(importedId);
            const bool importRedo = m_History.Redo(m_Scene) && m_Scene.FindModel(importedId) &&
                m_Scene.FindModel(importedId)->GPU == m_GPU;
            Check(imported && importUndo && importRedo, "asynchronous import undo/redo");
            m_Scene.SetNextEntityId(123);

            const auto folder = root / "bin-int" / "smoke-documents";
            std::filesystem::create_directories(folder);
            for (size_t i = 0; i < 9; ++i) m_Files[i] = folder / ("scene-" + std::to_string(i) + ".atomscene");
            m_BadFile = folder / "superseded.atomscene";
            std::string error;
            if (!SaveSceneDocument(m_Scene, m_Files[0], root, error)) throw std::runtime_error(error);
            const auto before = Bytes(m_Files[0]);
            Scene invalid;
            invalid.AddModel(Model{});
            Check(!SaveSceneDocument(invalid, m_Files[0], root, error) && !error.empty() &&
                Bytes(m_Files[0]) == before, "failed atomic save preserves existing file");
            const auto valid = Json::parse(before);
            auto oldVersion = valid;
            oldVersion["version"] = 1;
            oldVersion.erase("nextEntityId");
            for (auto& item : oldVersion["models"]) item.erase("id");
            Write(m_Files[1], oldVersion);
            auto duplicate = valid;
            duplicate["models"][1]["id"] = duplicate["models"][0]["id"];
            Write(m_Files[2], duplicate);
            auto transform = valid;
            transform["models"][0]["rotation"] = {0, 0, 0, 0};
            Write(m_Files[3], transform);
            auto missing = valid;
            missing["models"][1]["asset"] = "bin-int/smoke-documents/missing.gltf";
            Write(m_Files[4], missing);
            Write(m_Files[5], valid);
            Write(m_Files[6], valid);
            Write(m_BadFile, duplicate);
            auto empty = valid;
            empty["models"] = Json::array();
            Write(m_Files[7], empty);
            auto leaseScene = valid;
            const char* names[] = { "lease-a.glb", "lease-b.glb" };
            for (size_t i = 0; i < 2; ++i)
            {
                std::filesystem::copy_file(root / "Assets" / "Models" / "cube.glb", folder / names[i],
                    std::filesystem::copy_options::overwrite_existing);
                leaseScene["models"][i]["asset"] = PathToUtf8(folder / names[i]);
            }
            Write(m_Files[8], leaseScene);
        }
        catch (const std::exception& error)
        {
            ++m_Result.Errors;
            std::cerr << "EDITOR_CHECK_FAILED fixture: " << error.what() << '\n';
            m_Case = 9;
            assets.SetBudget(m_OriginalBudget);
        }
    }

    SceneDocumentChecks::~SceneDocumentChecks()
    {
        m_Assets.SetBudget(m_OriginalBudget);
    }

    bool SceneDocumentChecks::SceneUnchanged() const
    {
        return m_Scene.Revision() == m_Revision && m_Scene.Models.size() == m_ModelCount &&
            (m_Scene.Models.empty() || m_Scene.Models.front().Id == m_FirstId) &&
            m_State.SelectedEntity == m_Selection && m_History.CanUndo();
    }

    void SceneDocumentChecks::BeginCase()
    {
        m_History.Clear();
        m_History.RecordApplied("Sentinel", [](Scene&) {}, [](Scene&) {});
        m_State.SelectedEntity = m_Scene.Models.empty() ? InvalidEntityId : m_Scene.Models.front().Id;
        m_Revision = m_Scene.Revision();
        m_ModelCount = m_Scene.Models.size();
        m_FirstId = m_Scene.Models.empty() ? InvalidEntityId : m_Scene.Models.front().Id;
        m_Selection = m_State.SelectedEntity;
        m_Unchanged = true;
        if (m_Case == 6) m_Loader.Begin(m_BadFile);
        m_Loader.Begin(m_Files[m_Case]);
        if (m_Case == 5) m_Loader.Cancel();
        m_Pending = true;
    }

    void SceneDocumentChecks::Update(ModelAssetManager& assets)
    {
        if (m_Case >= 9) return;
        if (!m_Pending) BeginCase();
        m_Loader.Update(assets);
        m_Unchanged = m_Unchanged && SceneUnchanged();
        const auto status = m_Loader.Status();
        if (m_Loader.Busy() && status.State != SceneLoadState::Ready) return;
        bool passed = m_Unchanged && status.File == m_Files[m_Case];
        const bool success = m_Case == 0 || m_Case == 1 || m_Case == 6 || m_Case == 7 || m_Case == 8;
        if (success)
        {
            passed = passed && status.State == SceneLoadState::Ready &&
                m_Loader.Apply(m_Scene, m_History, m_State.SelectedEntity, assets);
            passed = passed && m_Scene.Revision() > m_Revision && !m_History.CanUndo() && !m_History.CanRedo() &&
                m_Loader.Status().State == SceneLoadState::Applied;
            if (m_Case == 7)
                passed = passed && m_Scene.Models.empty() && m_State.SelectedEntity == InvalidEntityId;
            else if (m_Case == 8)
                passed = passed && m_Scene.Models.size() == 2 && assets.CacheStats().OverBudget() &&
                    m_Scene.Models[0].GPU && m_Scene.Models[1].GPU &&
                    m_Scene.Models[0].GPU != m_Scene.Models[1].GPU &&
                    PathFromUtf8(m_Scene.Models[0].Asset->SourcePath).filename() == "lease-a.glb" &&
                    PathFromUtf8(m_Scene.Models[1].Asset->SourcePath).filename() == "lease-b.glb";
            else
            {
                passed = passed && m_Scene.Models.size() == 2;
                if (m_Scene.Models.size() == 2)
                {
                    const auto& first = m_Scene.Models[0];
                    const auto& second = m_Scene.Models[1];
                    const auto position = first.Transform.transform.GetTranslation();
                    passed = passed && first.Asset == m_Asset && first.GPU == m_GPU && second.GPU == first.GPU &&
                        first.InstanceName == "Roundtrip model" && first.Transform.scale == 2.5f &&
                        position.GetX() == 1.0f && position.GetY() == 2.0f && position.GetZ() == 3.0f &&
                        m_State.SelectedEntity == first.Id && first.Id != second.Id;
                    if (m_Case != 1) passed = passed && first.Id == 41 && second.Id == 42 && m_Scene.GetNextEntityId() == 123;
                    else passed = passed && first.Id != InvalidEntityId && second.Id != InvalidEntityId;
                }
            }
        }
        else
        {
            const auto expected = m_Case == 5 ? SceneLoadState::Cancelled : SceneLoadState::Failed;
            passed = passed && status.State == expected && (m_Case == 5 || !status.Error.empty()) &&
                !m_Loader.Apply(m_Scene, m_History, m_State.SelectedEntity, assets) && SceneUnchanged();
        }
        const char* labels[] = { "version 2 roundtrip", "version 1 migration", "duplicate IDs",
            "invalid transform", "partial asset failure", "cancel", "superseded load", "empty scene",
            "new multi-asset scene transaction survives zero cache budgets" };
        Check(passed, labels[m_Case]);
        ++m_Case;
        if (m_Case == 9) assets.SetBudget(m_OriginalBudget);
        m_Pending = false;
    }
}
