#pragma once

#include "Model.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

using AssetRequestId = uint64_t;

// Copyable request lease: keeping it protects a ready cache entry while a
// consumer waits for other assets. IDs alone are diagnostic lookup keys.
class AssetRequest
{
public:
    AssetRequest() = default;
    operator AssetRequestId() const { return m_Id; }
    AssetRequestId Id() const { return m_Id; }
private:
    friend class ModelAssetManager;
    AssetRequest(AssetRequestId id, std::shared_ptr<const void> lease)
        : m_Id(id), m_Lease(std::move(lease)) {}
    AssetRequestId m_Id = 0;
    std::shared_ptr<const void> m_Lease;
};

struct AssetCacheBudget
{
    uint64_t CpuBytes = 512ull * 1024 * 1024;
    uint64_t GpuBytes = 1024ull * 1024 * 1024;
};

struct AssetCacheStats
{
    AssetCacheBudget Budget;
    size_t Entries = 0;
    size_t ReadyEntries = 0;
    size_t ProtectedEntries = 0;
    uint64_t CpuBytes = 0; // Estimated retained CPU vector/string storage.
    uint64_t GpuBytes = 0; // Committed geometry and image allocations.
    uint64_t Evictions = 0;
    bool OverBudget() const { return CpuBytes > Budget.CpuBytes || GpuBytes > Budget.GpuBytes; }
};

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

    AssetRequest Request(const std::string& sourcePath);
    bool Cancel(AssetRequestId id);
    AssetLoadSnapshot Snapshot(AssetRequestId id) const;
    void Update();
    LoadedModelData LoadBlocking(const std::string& sourcePath, std::string* error = nullptr);
    // Soft limits: requests, scene/history owners and unfinished GPU frames are
    // never evicted. Call these on the render thread, as with Update.
    void SetBudget(AssetCacheBudget budget);
    void Trim();
    AssetCacheStats CacheStats() const;
    void Shutdown();
    static const char* StateName(AssetLoadState state);

private:
    struct Record
    {
        AssetLoadSnapshot Snapshot;
        std::atomic_bool Cancelled{ false };
        uint64_t LastUse = 0;
        uint64_t CpuBytes = 0;
    };

    void WorkerLoop();
    static std::string CanonicalPath(const std::string& path);
    static uint64_t EstimateCpuBytes(const ModelAsset& asset);
    static bool Protected(const std::shared_ptr<Record>& record);
    AssetCacheStats CacheStatsLocked() const;
    void TrimLocked();

    mutable std::mutex m_Mutex;
    std::condition_variable m_Changed;
    std::unordered_map<AssetRequestId, std::shared_ptr<Record>> m_Records;
    std::unordered_map<std::string, AssetRequestId> m_PathRequests;
    std::deque<std::shared_ptr<Record>> m_Queue;
    AssetRequestId m_NextId = 1;
    uint64_t m_UseSequence = 0;
    uint64_t m_Evictions = 0;
    AssetCacheBudget m_Budget;
    bool m_Stopping = false;
    std::thread m_Worker;
};
