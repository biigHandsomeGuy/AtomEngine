#pragma once

#include "Model.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

using AssetRequestId = uint64_t;

enum class AssetLoadState { Queued, Decoding, ReadyForUpload, Uploading, Ready, Failed, Cancelled };

struct AssetLoadSnapshot
{
    AssetRequestId Id = 0;
    AssetLoadState State = AssetLoadState::Failed;
    std::string SourcePath;
    std::string Error;
    std::string Warnings;
    bool CancelRequested = false;
    uint64_t UploadFence = 0;
    LoadedModelData Data;
};

// CPU worker never touches graphics state. Update, LoadBlocking and Shutdown run
// on the render thread; ready cache entries share immutable CPU/GPU residency.
class ModelAssetManager
{
public:
    ModelAssetManager();
    ~ModelAssetManager();
    ModelAssetManager(const ModelAssetManager&) = delete;
    ModelAssetManager& operator=(const ModelAssetManager&) = delete;

    AssetRequestId Request(const std::string& sourcePath);
    bool Cancel(AssetRequestId id);
    AssetLoadSnapshot Snapshot(AssetRequestId id) const;
    void Update();
    LoadedModelData LoadBlocking(const std::string& sourcePath, std::string* error = nullptr);
    void Shutdown();
    static const char* StateName(AssetLoadState state);

private:
    struct Record
    {
        AssetLoadSnapshot Snapshot;
        std::atomic_bool Cancelled{ false };
    };

    void WorkerLoop();
    static std::string CanonicalPath(const std::string& path);

    mutable std::mutex m_Mutex;
    std::condition_variable m_Changed;
    std::unordered_map<AssetRequestId, std::shared_ptr<Record>> m_Records;
    std::unordered_map<std::string, AssetRequestId> m_PathRequests;
    std::deque<std::shared_ptr<Record>> m_Queue;
    AssetRequestId m_NextId = 1;
    bool m_Stopping = false;
    std::thread m_Worker;
};
