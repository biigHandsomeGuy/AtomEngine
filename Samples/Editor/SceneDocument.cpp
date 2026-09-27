#include "pch.h"
#include "SceneDocument.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
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
            if (id == InvalidEntityId || id >= UINT64_MAX - 1)
                throw std::runtime_error("Entity ID is outside the supported range.");
            return id;
        }

        EntityId ReadNextId(const Json& value)
        {
            if ((!value.is_number_unsigned() && (!value.is_number_integer() || value.get<int64_t>() <= 0)) ||
                value.get<EntityId>() == InvalidEntityId || value.get<EntityId>() == UINT64_MAX)
                throw std::runtime_error("Next entity ID must be a positive allocation cursor.");
            return value.get<EntityId>();
        }

        void ValidateTransform(const SceneEntityRecord& model)
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


        SceneEntityRecord ReadEntity(const Json& item, int version, std::unordered_set<EntityId>& ids, EntityId& next)
        {
            SceneEntityRecord record;
            record.Id = version == 1 ? next++ : ReadId(item.at("id"));
            if (!ids.insert(record.Id).second) throw std::runtime_error("Scene contains duplicate entity IDs.");
            next = std::max(next, record.Id + 1);
            record.Name = item.value("name", std::string{});
            const auto& translation = item.at("translation");
            const auto& rotation = item.at("rotation");
            if (!translation.is_array() || translation.size() != 3 || !rotation.is_array() || rotation.size() != 4)
                throw std::runtime_error("Transform arrays must contain three translation and four rotation values.");
            for (size_t i = 0; i < 3; ++i) record.Translation[i] = translation.at(i).get<float>();
            for (size_t i = 0; i < 4; ++i) record.Rotation[i] = rotation.at(i).get<float>();
            record.Scale = item.value("scale", 1.0f);
            ValidateTransform(record);
            double length = 0;
            for (float value : record.Rotation) length += double(value) * value;
            for (float& value : record.Rotation) value = float(value / std::sqrt(length));
            record.Scale = std::max(0.001f, record.Scale);
            return record;
        }

        template<class T> SceneEntityRecord CaptureEntity(const T& item)
        {
            SceneEntityRecord record;
            record.Id = item.Id;
            record.Name = item.InstanceName;
            const auto position = item.Transform.transform.GetTranslation();
            DirectX::XMFLOAT4 rotation;
            DirectX::XMStoreFloat4(&rotation, static_cast<DirectX::XMVECTOR>(item.Transform.transform.GetRotation()));
            record.Translation = { position.GetX(), position.GetY(), position.GetZ() };
            record.Rotation = { rotation.x, rotation.y, rotation.z, rotation.w };
            record.Scale = item.Transform.scale;
            ValidateTransform(record);
            return record;
        }

        void ApplyEntity(TransformComponent& transform, const SceneEntityRecord& record)
        {
            transform.transform.SetTranslation(Math::Vector3(record.Translation[0], record.Translation[1], record.Translation[2]));
            transform.transform.SetRotation(Math::Quaternion(DirectX::XMVectorSet(
                record.Rotation[0], record.Rotation[1], record.Rotation[2], record.Rotation[3])));
            transform.scale = record.Scale;
        }

        void ValidateCamera(const CameraComponent& camera)
        {
            if (!std::isfinite(camera.VerticalFovDegrees) || camera.VerticalFovDegrees < 1 || camera.VerticalFovDegrees > 179 ||
                !std::isfinite(camera.NearClip) || !std::isfinite(camera.FarClip) || camera.NearClip <= 0 || camera.FarClip <= camera.NearClip)
                throw std::runtime_error("Camera requires a 1-179 degree FOV and finite 0 < near < far clips.");
        }

        void ValidateLight(const DirectionalLightComponent& light)
        {
            const float values[] = { light.Color.x, light.Color.y, light.Color.z, light.Intensity,
                light.ShadowCenter.x, light.ShadowCenter.y, light.ShadowCenter.z,
                light.ShadowBounds.x, light.ShadowBounds.y, light.ShadowBounds.z };
            for (const auto value : values)
                if (!std::isfinite(value)) throw std::runtime_error("Light parameters must be finite.");
            if (light.Color.x < 0 || light.Color.y < 0 || light.Color.z < 0 || light.Intensity < 0 ||
                light.ShadowBounds.x <= 0 || light.ShadowBounds.y <= 0 || light.ShadowBounds.z <= 0)
                throw std::runtime_error("Light color/intensity must be nonnegative and shadow bounds must be positive.");
            if (!std::isfinite(double(light.Color.x) * light.Intensity) ||
                double(std::max({light.Color.x, light.Color.y, light.Color.z})) * light.Intensity > FLT_MAX)
                throw std::runtime_error("Light radiance is outside the supported range.");
        }

        DirectX::XMFLOAT3 ReadVector3(const Json& item)
        {
            if (!item.is_array() || item.size() != 3) throw std::runtime_error("Vector parameters require three values.");
            return { item.at(0).get<float>(), item.at(1).get<float>(), item.at(2).get<float>() };
        }
        Json WriteVector3(const DirectX::XMFLOAT3& value) { return Json::array({value.x, value.y, value.z}); }
        EntityId ReadReference(const Json& value) { return value.is_number_integer() && value == 0 ? InvalidEntityId : ReadId(value); }
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
                (document.at("version") != 1 && document.at("version") != 2 && document.at("version") != 3))
                throw std::runtime_error("Scene version must be an integer.");
            parsed.Version = document.at("version").get<int>();
            if (document.at("format").get<std::string>() != "AtomEngineScene" ||
                (parsed.Version != 1 && parsed.Version != 2 && parsed.Version != 3) || !document.at("models").is_array())
                throw std::runtime_error("Unsupported or malformed AtomEngine scene document.");
            if (parsed.Version >= 2 && document.contains("nextEntityId"))
                parsed.NextEntityId = ReadNextId(document.at("nextEntityId"));
            std::unordered_set<EntityId> ids;
            for (const auto& item : document.at("models"))
            {
                if (cancelled && cancelled->load()) throw std::runtime_error("Scene load cancelled.");
                SceneModelRecord model;
                static_cast<SceneEntityRecord&>(model) = ReadEntity(item, parsed.Version, ids, parsed.NextEntityId);
                const auto source = item.at("asset").get<std::string>();
                if (source.empty()) throw std::runtime_error("A scene model has an empty asset path.");
                auto path = PathFromUtf8(source);
                if (path.is_relative()) path = root / path;
                model.AssetPath = PathToUtf8(path.lexically_normal());
                model.Name = item.value("name", PathToUtf8(path.stem()));
                parsed.Models.push_back(std::move(model));
            }
            if (parsed.Version == 3)
            {
                if (!document.at("cameras").is_array() || !document.at("lights").is_array())
                    throw std::runtime_error("Scene cameras and lights must be arrays.");
                for (const auto& item : document.at("cameras"))
                {
                    if (cancelled && cancelled->load()) throw std::runtime_error("Scene load cancelled.");
                    SceneCameraRecord camera;
                    static_cast<SceneEntityRecord&>(camera) = ReadEntity(item, 3, ids, parsed.NextEntityId);
                    camera.Camera = { item.at("verticalFovDegrees").get<float>(), item.at("nearClip").get<float>(), item.at("farClip").get<float>() };
                    ValidateCamera(camera.Camera);
                    parsed.Cameras.push_back(std::move(camera));
                }
                for (const auto& item : document.at("lights"))
                {
                    if (cancelled && cancelled->load()) throw std::runtime_error("Scene load cancelled.");
                    if (item.at("type") != "directional") throw std::runtime_error("Unsupported scene light type.");
                    SceneLightRecord light;
                    static_cast<SceneEntityRecord&>(light) = ReadEntity(item, 3, ids, parsed.NextEntityId);
                    light.Light.Color = ReadVector3(item.at("color"));
                    light.Light.Intensity = item.at("intensity").get<float>();
                    light.Light.Enabled = item.at("enabled").get<bool>();
                    light.Light.CastsShadows = item.at("castsShadows").get<bool>();
                    light.Light.ShadowCenter = ReadVector3(item.at("shadowCenter"));
                    light.Light.ShadowBounds = ReadVector3(item.at("shadowBounds"));
                    ValidateLight(light.Light);
                    parsed.Lights.push_back(std::move(light));
                }
                parsed.ActiveCamera = ReadReference(document.at("activeCamera"));
                parsed.ActiveLight = ReadReference(document.at("activeLight"));
                if ((parsed.ActiveCamera && std::none_of(parsed.Cameras.begin(), parsed.Cameras.end(), [&](const auto& item) { return item.Id == parsed.ActiveCamera; })) ||
                    (parsed.ActiveLight && std::none_of(parsed.Lights.begin(), parsed.Lights.end(), [&](const auto& item) { return item.Id == parsed.ActiveLight; })))
                    throw std::runtime_error("Active camera/light must reference the correct entity type.");
            }
            else if (!parsed.Models.empty())
            {
                // Model-only documents migrate to explicit component defaults.
                CameraEntity camera;
                camera.Id = parsed.NextEntityId++;
                Math::Camera pose;
                pose.SetEyeAtUp(Math::Vector3(0, 0, -20), Math::Vector3(Math::kZero), Math::Vector3(Math::kYUnitVector));
                camera.Transform.transform.SetTranslation(pose.GetPosition());
                camera.Transform.transform.SetRotation(pose.GetRotation());
                SceneCameraRecord record;
                static_cast<SceneEntityRecord&>(record) = CaptureEntity(camera);
                record.Camera = camera.Camera;
                parsed.ActiveCamera = camera.Id;
                parsed.Cameras.push_back(std::move(record));
                auto sun = Scene::DefaultSun();
                sun.Id = parsed.NextEntityId++;
                SceneLightRecord light;
                static_cast<SceneEntityRecord&>(light) = CaptureEntity(sun);
                light.Light = sun.Light;
                parsed.ActiveLight = sun.Id;
                parsed.Lights.push_back(std::move(light));
            }
            ReadNextId(Json(parsed.NextEntityId));
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
            Json document = { {"format", "AtomEngineScene"}, {"version", 3},
                {"nextEntityId", scene.GetNextEntityId()}, {"models", Json::array()}, {"cameras", Json::array()},
                {"lights", Json::array()}, {"activeCamera", scene.ActiveCameraId()}, {"activeLight", scene.ActiveLightId()} };
            ReadNextId(document.at("nextEntityId"));
            std::unordered_set<EntityId> ids;
            auto writeEntity = [&](const auto& item)
            {
                ReadId(Json(item.Id));
                if (!ids.insert(item.Id).second) throw std::runtime_error("Scene contains duplicate entity IDs.");
                const auto record = CaptureEntity(item);
                return Json{ {"id", record.Id}, {"name", record.Name}, {"translation", record.Translation},
                    {"rotation", record.Rotation}, {"scale", record.Scale} };
            };
            for (const auto& model : scene.Models)
            {
                auto item = writeEntity(model);
                item["asset"] = PathToUtf8(PortableAssetPath(model, root));
                document["models"].push_back(std::move(item));
            }
            for (const auto& camera : scene.Cameras)
            {
                ValidateCamera(camera.Camera);
                auto item = writeEntity(camera);
                item["verticalFovDegrees"] = camera.Camera.VerticalFovDegrees;
                item["nearClip"] = camera.Camera.NearClip;
                item["farClip"] = camera.Camera.FarClip;
                document["cameras"].push_back(std::move(item));
            }
            for (const auto& light : scene.Lights)
            {
                ValidateLight(light.Light);
                auto item = writeEntity(light);
                item["type"] = "directional";
                item["color"] = WriteVector3(light.Light.Color);
                item["intensity"] = light.Light.Intensity;
                item["enabled"] = light.Light.Enabled;
                item["castsShadows"] = light.Light.CastsShadows;
                item["shadowCenter"] = WriteVector3(light.Light.ShadowCenter);
                item["shadowBounds"] = WriteVector3(light.Light.ShadowBounds);
                document["lights"].push_back(std::move(item));
            }
            if ((scene.ActiveCameraId() && !scene.FindCamera(scene.ActiveCameraId())) ||
                (scene.ActiveLightId() && !scene.FindLight(scene.ActiveLightId())))
                throw std::runtime_error("Active camera/light must reference the correct entity type.");
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
            ApplyEntity(model.Transform, record);
            model.UpdateConstants();
            models.push_back(std::move(model));
            if (!resident.Warnings.empty()) job.Status.Warnings += resident.Warnings + "\n";
        }
        std::vector<CameraEntity> cameras;
        std::vector<DirectionalLightEntity> lights;
        cameras.reserve(job.Document.Cameras.size());
        lights.reserve(job.Document.Lights.size());
        for (const auto& record : job.Document.Cameras)
        {
            CameraEntity camera;
            camera.Id = record.Id; camera.InstanceName = record.Name; camera.Camera = record.Camera;
            ApplyEntity(camera.Transform, record);
            cameras.push_back(std::move(camera));
        }
        for (const auto& record : job.Document.Lights)
        {
            DirectionalLightEntity light;
            light.Id = record.Id; light.InstanceName = record.Name; light.Light = record.Light;
            ApplyEntity(light.Transform, record);
            lights.push_back(std::move(light));
        }
        scene.ReplaceContent(std::move(models), std::move(cameras), std::move(lights),
            job.Document.ActiveCamera, job.Document.ActiveLight, job.Document.NextEntityId);
        history.Clear();
        selection = scene.FirstEntityId();
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
