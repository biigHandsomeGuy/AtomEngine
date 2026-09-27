#pragma once

#include "SceneGraph.h"
#include "ModelAssetManager.h"
#include "EditorCommandHistory.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace AtomEditor
{
    struct SceneEntityRecord
    {
        EntityId Id = InvalidEntityId;
        std::string Name;
        std::array<float, 3> Translation{};
        std::array<float, 4> Rotation{ 0, 0, 0, 1 };
        float Scale = 1.0f;
    };
    struct SceneModelRecord : SceneEntityRecord { std::string AssetPath; std::map<uint32_t, MaterialParameters> MaterialOverrides; };
    struct SceneCameraRecord : SceneEntityRecord { CameraComponent Camera; };
    struct SceneLightRecord : SceneEntityRecord { DirectionalLightComponent Light; };

    struct SceneDocumentData
    {
        int Version = 4;
        EntityId NextEntityId = 1;
        std::vector<SceneModelRecord> Models;
        std::vector<SceneCameraRecord> Cameras;
        std::vector<SceneLightRecord> Lights;
        EntityId ActiveCamera = InvalidEntityId;
        EntityId ActiveLight = InvalidEntityId;
    };

    bool ReadSceneDocument(const std::filesystem::path& file, const std::filesystem::path& projectRoot,
        SceneDocumentData& document, std::string& error, const std::atomic_bool* cancelled = nullptr);
    bool SaveSceneDocument(const Scene& scene, const std::filesystem::path& file,
        const std::filesystem::path& projectRoot, std::string& error);

    enum class SceneLoadState { Idle, Queued, Reading, Parsed, LoadingAssets, Ready, Applied, Failed, Cancelled };

    struct SceneLoadStatus
    {
        SceneLoadState State = SceneLoadState::Idle;
        std::filesystem::path File;
        std::string Error;
        std::string Warnings;
        size_t ReadyModels = 0;
        size_t TotalModels = 0;
    };

    // JSON parsing happens on a worker. Asset requests and atomic scene commit
    // happen on the render thread. Cancellation detaches shared asset requests.
    class SceneDocumentLoader
    {
    public:
        explicit SceneDocumentLoader(std::filesystem::path projectRoot);
        ~SceneDocumentLoader();
        SceneDocumentLoader(const SceneDocumentLoader&) = delete;
        SceneDocumentLoader& operator=(const SceneDocumentLoader&) = delete;

        void Begin(const std::filesystem::path& file);
        void Cancel();
        void Update(ModelAssetManager& assets);
        bool Apply(Scene& scene, CommandHistory& history, EntityId& selection, ModelAssetManager& assets);
        SceneLoadStatus Status() const;
        bool Busy() const;
        void Shutdown();

    private:
        struct Job
        {
            SceneLoadStatus Status;
            SceneDocumentData Document;
            std::vector<AssetRequest> Assets;
            std::unordered_map<std::string, AssetRequest> RequestedPaths;
            std::atomic_bool Cancelled{ false };
        };

        void WorkerLoop();
        std::filesystem::path m_ProjectRoot;
        mutable std::mutex m_Mutex;
        std::condition_variable m_Changed;
        std::shared_ptr<Job> m_Active;
        std::deque<std::shared_ptr<Job>> m_Queue;
        bool m_Stopping = false;
        std::thread m_Worker;
    };
}
