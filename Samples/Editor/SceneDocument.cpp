#include "pch.h"
#include "SceneDocument.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include "PathUtils.h"
#include "../../Dependencies/tinygltf/json.hpp"

namespace AtomEditor
{
    namespace
    {
        using Json = nlohmann::json;

        EntityId ReadId(const Json& value)
        {
            if (!value.is_number_unsigned() && (!value.is_number_integer() || value.get<int64_t>() <= 0))
                throw std::runtime_error("Entity IDs must be positive integers.");
            const auto id = value.get<EntityId>();
            if (id == InvalidEntityId || id == UINT64_MAX)
                throw std::runtime_error("Entity ID is outside the supported range.");
            return id;
        }

        void ValidateTransform(const SceneModelRecord& model)
        {
            for (float value : model.Translation)
                if (!std::isfinite(value)) throw std::runtime_error("Translation must contain finite numbers.");
            double length = 0;
            for (float value : model.Rotation)
            {
                if (!std::isfinite(value)) throw std::runtime_error("Rotation must contain finite numbers.");
                length += double(value) * value;
            }
            if (length < 1e-12) throw std::runtime_error("Rotation quaternion must have nonzero length.");
            if (!std::isfinite(model.Scale) || model.Scale <= 0)
                throw std::runtime_error("Scale must be finite and positive.");
        }

        std::filesystem::path PortableAssetPath(const Model& model, const std::filesystem::path& root)
        {
            if (!model.Asset || model.Asset->SourcePath.empty())
                throw std::runtime_error("A scene model has no source asset.");
            auto path = PathFromUtf8(model.Asset->SourcePath);
            std::error_code error;
            auto relative = std::filesystem::relative(path, root, error);
            if (!error && !relative.empty() && *relative.begin() != "..") return relative;
            return path;
        }
    }

    bool ReadSceneDocument(const std::filesystem::path& file, const std::filesystem::path& root,
        SceneDocumentData& result, std::string& error, const std::atomic_bool* cancelled)
    {
        error.clear();
        try
        {
            if (cancelled && cancelled->load()) throw std::runtime_error("Scene load cancelled.");
            std::ifstream input(file, std::ios::binary);
            if (!input) throw std::runtime_error("Could not open the scene document.");
            Json document;
            input >> document;
            SceneDocumentData parsed;
            if (!document.at("version").is_number_integer() ||
                (document.at("version") != 1 && document.at("version") != 2))
                throw std::runtime_error("Scene version must be an integer.");
            parsed.Version = document.at("version").get<int>();
            if (document.at("format").get<std::string>() != "AtomEngineScene" ||
                (parsed.Version != 1 && parsed.Version != 2) || !document.at("models").is_array())
                throw std::runtime_error("Unsupported or malformed AtomEngine scene document.");
            if (parsed.Version == 2 && document.contains("nextEntityId"))
                parsed.NextEntityId = ReadId(document.at("nextEntityId"));
            std::unordered_set<EntityId> ids;
            for (const auto& item : document.at("models"))
            {
                if (cancelled && cancelled->load()) throw std::runtime_error("Scene load cancelled.");
                SceneModelRecord model;
                if (parsed.Version == 2)
                {
                    model.Id = ReadId(item.at("id"));
                    if (!ids.insert(model.Id).second) throw std::runtime_error("Scene contains duplicate entity IDs.");
                    parsed.NextEntityId = std::max(parsed.NextEntityId, model.Id + 1);
                }
                const auto source = item.at("asset").get<std::string>();
                if (source.empty()) throw std::runtime_error("A scene model has an empty asset path.");
                auto path = PathFromUtf8(source);
                if (path.is_relative()) path = root / path;
                model.AssetPath = PathToUtf8(path.lexically_normal());
                model.Name = item.value("name", PathToUtf8(path.stem()));
                const auto& translation = item.at("translation");
                const auto& rotation = item.at("rotation");
                if (!translation.is_array() || translation.size() != 3 || !rotation.is_array() || rotation.size() != 4)
                    throw std::runtime_error("Transform arrays must contain three translation and four rotation values.");
                for (size_t i = 0; i < 3; ++i) model.Translation[i] = translation.at(i).get<float>();
                for (size_t i = 0; i < 4; ++i) model.Rotation[i] = rotation.at(i).get<float>();
                model.Scale = item.value("scale", 1.0f);
                ValidateTransform(model);
                double length = 0;
                for (float value : model.Rotation) length += double(value) * value;
                const double inverseLength = 1.0 / std::sqrt(length);
                for (float& value : model.Rotation) value = float(value * inverseLength);
                model.Scale = std::max(0.001f, model.Scale);
                parsed.Models.push_back(std::move(model));
            }
            ReadId(Json(parsed.NextEntityId));
            result = std::move(parsed);
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool SaveSceneDocument(const Scene& scene, const std::filesystem::path& file,
        const std::filesystem::path& root, std::string& error)
    {
        error.clear();
        std::filesystem::path temporary;
        try
        {
            Json document = { {"format", "AtomEngineScene"}, {"version", 2},
                {"nextEntityId", scene.GetNextEntityId()}, {"models", Json::array()} };
            ReadId(document.at("nextEntityId"));
            std::unordered_set<EntityId> ids;
            for (const auto& model : scene.Models)
            {
                if (model.Id == InvalidEntityId || model.Id == UINT64_MAX || !ids.insert(model.Id).second)
                    throw std::runtime_error("Scene contains invalid or duplicate entity IDs.");
                SceneModelRecord record;
                const auto position = model.Transform.transform.GetTranslation();
                DirectX::XMFLOAT4 rotation;
                DirectX::XMStoreFloat4(&rotation, static_cast<DirectX::XMVECTOR>(model.Transform.transform.GetRotation()));
                record.Translation = { position.GetX(), position.GetY(), position.GetZ() };
                record.Rotation = { rotation.x, rotation.y, rotation.z, rotation.w };
                record.Scale = model.Transform.scale;
                ValidateTransform(record);
                document["models"].push_back({ {"id", model.Id}, {"name", model.InstanceName},
                    {"asset", PathToUtf8(PortableAssetPath(model, root))}, {"translation", record.Translation},
                    {"rotation", record.Rotation}, {"scale", record.Scale} });
            }
            static std::atomic_uint64_t sequence{ 0 };
            temporary = file;
            temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence);
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Could not create the scene's temporary file.");
            output << document.dump(2);
            output.close();
            if (!output) throw std::runtime_error("Could not write the scene document.");
            if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Could not replace the scene document: " + std::error_code(GetLastError(), std::system_category()).message());
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            if (!temporary.empty())
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
            }
            return false;
        }
    }

    SceneDocumentLoader::SceneDocumentLoader(std::filesystem::path root)
        : m_ProjectRoot(std::move(root)), m_Worker([this] { WorkerLoop(); }) {}
    SceneDocumentLoader::~SceneDocumentLoader() { Shutdown(); }

    void SceneDocumentLoader::Begin(const std::filesystem::path& file)
    {
        Cancel();
        std::lock_guard lock(m_Mutex);
        if (m_Stopping) throw std::runtime_error("Scene loader has shut down.");
        auto job = std::make_shared<Job>();
        job->Status.State = SceneLoadState::Queued;
        job->Status.File = file;
        m_Active = job;
        m_Queue.push_back(std::move(job));
        m_Changed.notify_all();
    }

    void SceneDocumentLoader::Cancel()
    {
        std::lock_guard lock(m_Mutex);
        if (!m_Active || m_Active->Status.State == SceneLoadState::Applied || m_Active->Status.State == SceneLoadState::Failed)
            return;
        m_Active->Cancelled.store(true);
        m_Active->Status.State = SceneLoadState::Cancelled;
        m_Active->Assets.clear();
        m_Active->RequestedPaths.clear();
    }

    void SceneDocumentLoader::WorkerLoop()
    {
        for (;;)
        {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(m_Mutex);
                m_Changed.wait(lock, [this] { return m_Stopping || !m_Queue.empty(); });
                if (m_Stopping) return;
                job = m_Queue.front();
                m_Queue.pop_front();
                if (job->Cancelled.load()) continue;
                job->Status.State = SceneLoadState::Reading;
            }
            SceneDocumentData document;
            std::string error;
            const bool loaded = ReadSceneDocument(job->Status.File, m_ProjectRoot, document, error, &job->Cancelled);
            std::lock_guard lock(m_Mutex);
            if (job->Cancelled.load()) continue;
            if (!loaded)
            {
                job->Status.Error = std::move(error);
                job->Status.State = SceneLoadState::Failed;
            }
            else
            {
                job->Document = std::move(document);
                job->Status.TotalModels = job->Document.Models.size();
                job->Status.State = SceneLoadState::Parsed;
            }
        }
    }

    void SceneDocumentLoader::Update(ModelAssetManager& assets)
    {
        std::lock_guard lock(m_Mutex);
        if (!m_Active) return;
        auto& job = *m_Active;
        if (job.Status.State == SceneLoadState::Parsed)
        {
            job.Status.State = SceneLoadState::LoadingAssets;
        }
        if (job.Status.State != SceneLoadState::LoadingAssets) return;
        const size_t requestLimit = std::min(job.Document.Models.size(), job.Assets.size() + 64);
        while (job.Assets.size() < requestLimit)
        {
            const auto& path = job.Document.Models[job.Assets.size()].AssetPath;
            auto found = job.RequestedPaths.find(path);
            if (found == job.RequestedPaths.end()) found = job.RequestedPaths.emplace(path, assets.Request(path)).first;
            job.Assets.push_back(found->second);
        }
        job.Status.ReadyModels = 0;
        std::unordered_map<AssetRequestId, AssetLoadSnapshot> snapshots;
        for (const auto id : job.Assets)
        {
            auto found = snapshots.find(id);
            if (found == snapshots.end()) found = snapshots.emplace(id, assets.Snapshot(id)).first;
            const auto& asset = found->second;
            if (asset.State == AssetLoadState::Failed || asset.State == AssetLoadState::Cancelled)
            {
                job.Status.Error = "Failed to load " + asset.SourcePath + ": " +
                    (asset.Error.empty() ? "Asset request cancelled." : asset.Error);
                job.Status.State = SceneLoadState::Failed;
                job.Assets.clear();
                job.RequestedPaths.clear();
                return;
            }
            if (asset.State == AssetLoadState::Ready) ++job.Status.ReadyModels;
        }
        if (job.Status.ReadyModels == job.Status.TotalModels) job.Status.State = SceneLoadState::Ready;
    }

    bool SceneDocumentLoader::Apply(Scene& scene, CommandHistory& history, EntityId& selection, ModelAssetManager& assets)
    {
        std::lock_guard lock(m_Mutex);
        if (!m_Active || m_Active->Status.State != SceneLoadState::Ready) return false;
        auto& job = *m_Active;
        std::vector<Model> models;
        models.reserve(job.Document.Models.size());
        for (size_t i = 0; i < job.Document.Models.size(); ++i)
        {
            auto resident = assets.Snapshot(job.Assets[i]);
            if (resident.State != AssetLoadState::Ready) return false;
            auto model = ModelUtility::CreateModel(std::move(resident.Data));
            const auto& record = job.Document.Models[i];
            model.Id = record.Id;
            model.InstanceName = record.Name;
            model.Transform.transform.SetTranslation(Math::Vector3(record.Translation[0], record.Translation[1], record.Translation[2]));
            model.Transform.transform.SetRotation(Math::Quaternion(DirectX::XMVectorSet(
                record.Rotation[0], record.Rotation[1], record.Rotation[2], record.Rotation[3])));
            model.Transform.scale = record.Scale;
            model.UpdateConstants();
            models.push_back(std::move(model));
            if (!resident.Warnings.empty()) job.Status.Warnings += resident.Warnings + "\n";
        }
        scene.ReplaceModels(std::move(models));
        scene.SetNextEntityId(job.Document.NextEntityId);
        history.Clear();
        selection = scene.Models.empty() ? InvalidEntityId : scene.Models.front().Id;
        job.Status.State = SceneLoadState::Applied;
        job.Assets.clear();
        job.RequestedPaths.clear();
        return true;
    }

    SceneLoadStatus SceneDocumentLoader::Status() const
    {
        std::lock_guard lock(m_Mutex);
        return m_Active ? m_Active->Status : SceneLoadStatus{};
    }

    bool SceneDocumentLoader::Busy() const
    {
        const auto state = Status().State;
        return state == SceneLoadState::Queued || state == SceneLoadState::Reading || state == SceneLoadState::Parsed ||
            state == SceneLoadState::LoadingAssets || state == SceneLoadState::Ready;
    }

    void SceneDocumentLoader::Shutdown()
    {
        {
            std::lock_guard lock(m_Mutex);
            if (m_Stopping) return;
            m_Stopping = true;
            if (m_Active) m_Active->Cancelled.store(true);
            for (auto& job : m_Queue) job->Cancelled.store(true);
        }
        m_Changed.notify_all();
        if (m_Worker.joinable()) m_Worker.join();
        std::lock_guard lock(m_Mutex);
        m_Queue.clear();
        m_Active.reset();
    }
}
