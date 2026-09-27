#include "pch.h"
#include "ModelAssetManager.h"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include "CommandContext.h"
#include "CommandListManager.h"
#include "GraphicsCore.h"
#include "PathUtils.h"

ModelAssetManager::ModelAssetManager() : m_Worker([this] { WorkerLoop(); }) {}
ModelAssetManager::~ModelAssetManager() { Shutdown(); }

std::string ModelAssetManager::CanonicalPath(const std::string& path)
{
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(PathFromUtf8(path), error);
    return PathToUtf8(error ? PathFromUtf8(path).lexically_normal() : canonical);
}

AssetRequest ModelAssetManager::Request(const std::string& path)
{
    const std::string source = CanonicalPath(path);
    std::lock_guard lock(m_Mutex);
    if (m_Stopping) throw std::runtime_error("The asset manager has shut down.");
    if (auto found = m_PathRequests.find(source); found != m_PathRequests.end())
    {
        const auto& record = m_Records.at(found->second);
        if (!record->Cancelled.load() && record->Snapshot.State != AssetLoadState::Failed)
        {
            record->LastUse = ++m_UseSequence;
            return AssetRequest(found->second, record);
        }
    }
    auto record = std::make_shared<Record>();
    record->Snapshot.Id = m_NextId++;
    record->Snapshot.SourcePath = source;
    record->Snapshot.State = AssetLoadState::Queued;
    record->LastUse = ++m_UseSequence;
    m_Records.emplace(record->Snapshot.Id, record);
    m_PathRequests[source] = record->Snapshot.Id;
    m_Queue.push_back(record);
    m_Changed.notify_all();
    return AssetRequest(record->Snapshot.Id, record);
}

bool ModelAssetManager::Cancel(AssetRequestId id)
{
    std::lock_guard lock(m_Mutex);
    auto found = m_Records.find(id);
    if (found == m_Records.end()) return false;
    auto& record = *found->second;
    if (record.Snapshot.State == AssetLoadState::Ready || record.Snapshot.State == AssetLoadState::Failed ||
        record.Snapshot.State == AssetLoadState::Cancelled) return false;
    record.Cancelled.store(true);
    record.Snapshot.CancelRequested = true;
    if (record.Snapshot.State != AssetLoadState::Uploading)
    {
        record.Snapshot.State = AssetLoadState::Cancelled;
        record.Snapshot.Data = {};
        record.CpuBytes = 0;
    }
    m_Changed.notify_all();
    return true;
}

AssetLoadSnapshot ModelAssetManager::Snapshot(AssetRequestId id) const
{
    std::lock_guard lock(m_Mutex);
    auto found = m_Records.find(id);
    auto snapshot = found == m_Records.end() ? AssetLoadSnapshot{} : found->second->Snapshot;
    if (snapshot.State != AssetLoadState::Ready)
        snapshot.Data.GPU.reset(); // Never publish drawable residency before its fence.
    return snapshot;
}

void ModelAssetManager::WorkerLoop()
{
    for (;;)
    {
        std::shared_ptr<Record> record;
        {
            std::unique_lock lock(m_Mutex);
            m_Changed.wait(lock, [this] { return m_Stopping || !m_Queue.empty(); });
            if (m_Stopping) return;
            record = m_Queue.front();
            m_Queue.pop_front();
            if (record->Cancelled.load()) continue;
            record->Snapshot.State = AssetLoadState::Decoding;
        }
        std::string error, warnings;
        auto asset = ModelUtility::DecodeGltfAsset(record->Snapshot.SourcePath, &error, &warnings, &record->Cancelled);
        {
            std::lock_guard lock(m_Mutex);
            record->Snapshot.Warnings = std::move(warnings);
            if (record->Cancelled.load())
                record->Snapshot.State = AssetLoadState::Cancelled;
            else if (!asset)
            {
                record->Snapshot.State = AssetLoadState::Failed;
                record->Snapshot.Error = std::move(error);
            }
            else
            {
                record->CpuBytes = EstimateCpuBytes(*asset);
                record->Snapshot.Data.Asset = std::move(asset);
                record->Snapshot.State = AssetLoadState::ReadyForUpload;
            }
        }
        m_Changed.notify_all();
    }
}

void ModelAssetManager::Update()
{
    std::lock_guard lock(m_Mutex);
    if (m_Stopping) return;
    bool submitted = false;
    for (auto& [id, pointer] : m_Records)
    {
        auto& record = *pointer;
        auto& snapshot = record.Snapshot;
        if (snapshot.State == AssetLoadState::Uploading && Graphics::g_CommandManager.IsFenceComplete(snapshot.UploadFence))
        {
            snapshot.State = record.Cancelled.load() ? AssetLoadState::Cancelled : AssetLoadState::Ready;
            if (record.Cancelled.load())
            {
                snapshot.Data = {};
                record.CpuBytes = 0;
            }
        }
        if (!submitted && snapshot.State == AssetLoadState::ReadyForUpload)
        {
            auto& context = CommandContext::Begin(L"Asset residency upload");
            snapshot.Data.GPU = ModelUtility::UploadAsset(*snapshot.Data.Asset, context);
            snapshot.UploadFence = context.Finish(false);
            snapshot.State = AssetLoadState::Uploading;
            submitted = true; // One complete asset submission per update.
        }
    }
    TrimLocked();
}

LoadedModelData ModelAssetManager::LoadBlocking(const std::string& path, std::string* error)
{
    const auto id = Request(path);
    for (;;)
    {
        Update();
        auto snapshot = Snapshot(id);
        if (snapshot.State == AssetLoadState::Ready) return snapshot.Data;
        if (snapshot.State == AssetLoadState::Failed || snapshot.State == AssetLoadState::Cancelled)
        {
            if (error) *error = snapshot.Error.empty() ? "Import cancelled." : snapshot.Error;
            return {};
        }
        std::unique_lock lock(m_Mutex);
        m_Changed.wait_for(lock, std::chrono::milliseconds(2));
    }
}

uint64_t ModelAssetManager::EstimateCpuBytes(const ModelAsset& asset)
{
    uint64_t bytes = sizeof(ModelAsset) + asset.Name.capacity() + asset.SourcePath.capacity();
    bytes += asset.Meshes.capacity() * sizeof(MeshAsset) + asset.Materials.capacity() * sizeof(MaterialAsset);
    bytes += asset.Images.capacity() * sizeof(ImageAsset);
    for (const auto& mesh : asset.Meshes)
        bytes += mesh.Name.capacity() + mesh.Vertices.capacity() * sizeof(Vertex) +
            mesh.Indices.capacity() * sizeof(uint32_t) + mesh.SubMeshes.capacity() * sizeof(SubMesh);
    for (const auto& material : asset.Materials) bytes += material.Name.capacity();
    for (const auto& image : asset.Images) bytes += image.Pixels.capacity();
    return bytes;
}

bool ModelAssetManager::Protected(const std::shared_ptr<Record>& record)
{
    return record.use_count() > 1 || record->Snapshot.State != AssetLoadState::Ready ||
        record->Snapshot.Data.Asset.use_count() > 1 || record->Snapshot.Data.GPU.use_count() > 1;
}

AssetCacheStats ModelAssetManager::CacheStatsLocked() const
{
    AssetCacheStats stats;
    stats.Budget = m_Budget;
    stats.Entries = m_Records.size();
    stats.Evictions = m_Evictions;
    for (const auto& [id, record] : m_Records)
    {
        if (record->Snapshot.Data.Asset) stats.CpuBytes += record->CpuBytes;
        if (record->Snapshot.Data.GPU) stats.GpuBytes += record->Snapshot.Data.GPU->ResidentBytes;
        if (record->Snapshot.State == AssetLoadState::Ready) ++stats.ReadyEntries;
        if (Protected(record)) ++stats.ProtectedEntries;
    }
    return stats;
}

AssetCacheStats ModelAssetManager::CacheStats() const
{
    std::lock_guard lock(m_Mutex);
    return CacheStatsLocked();
}

void ModelAssetManager::SetBudget(AssetCacheBudget budget)
{
    std::lock_guard lock(m_Mutex);
    m_Budget = budget;
    TrimLocked();
}

void ModelAssetManager::Trim()
{
    std::lock_guard lock(m_Mutex);
    TrimLocked();
}

void ModelAssetManager::TrimLocked()
{
    // Discard unobserved terminal diagnostics once no request lease needs them.
    // Decoding/queued work and pending GPU uploads always remain protected.
    for (auto it = m_Records.begin(); it != m_Records.end();)
    {
        const auto& record = it->second;
        if (record.use_count() == 1 && (record->Snapshot.State == AssetLoadState::Failed ||
            record->Snapshot.State == AssetLoadState::Cancelled))
        {
            const auto path = m_PathRequests.find(record->Snapshot.SourcePath);
            if (path != m_PathRequests.end() && path->second == it->first) m_PathRequests.erase(path);
            it = m_Records.erase(it);
        }
        else ++it;
    }
    auto stats = CacheStatsLocked();
    if (!stats.OverBudget()) return;
    std::vector<AssetRequestId> candidates;
    for (const auto& [id, record] : m_Records)
        if (!Protected(record)) candidates.push_back(id);
    std::sort(candidates.begin(), candidates.end(), [this](auto left, auto right)
    {
        return m_Records.at(left)->LastUse < m_Records.at(right)->LastUse;
    });
    for (const auto id : candidates)
    {
        if (!stats.OverBudget()) break;
        const auto& record = m_Records.at(id);
        stats.CpuBytes -= record->CpuBytes;
        stats.GpuBytes -= record->Snapshot.Data.GPU ? record->Snapshot.Data.GPU->ResidentBytes : 0;
        const auto path = m_PathRequests.find(record->Snapshot.SourcePath);
        if (path != m_PathRequests.end() && path->second == id) m_PathRequests.erase(path);
        m_Records.erase(id);
        ++m_Evictions;
    }
}

void ModelAssetManager::Shutdown()
{
    {
        std::lock_guard lock(m_Mutex);
        if (m_Stopping) return;
        m_Stopping = true;
        for (auto& [id, record] : m_Records)
            if (record->Snapshot.State != AssetLoadState::Ready) record->Cancelled.store(true);
    }
    m_Changed.notify_all();
    if (m_Worker.joinable()) m_Worker.join();
    for (auto& [id, record] : m_Records)
        if (record->Snapshot.UploadFence != 0 && !Graphics::g_CommandManager.IsFenceComplete(record->Snapshot.UploadFence))
            Graphics::g_CommandManager.WaitForFence(record->Snapshot.UploadFence);
    std::lock_guard lock(m_Mutex);
    // Outstanding request leases may outlive the service. Strip their cache
    // ownership now; consumers' scene/frame owners still manage their lifetimes.
    for (auto& [id, record] : m_Records)
    {
        record->Snapshot.Data = {};
        record->CpuBytes = 0;
    }
    m_Records.clear();
    m_PathRequests.clear();
    m_Queue.clear();
}

const char* ModelAssetManager::StateName(AssetLoadState state)
{
    switch (state)
    {
    case AssetLoadState::Queued: return "Queued";
    case AssetLoadState::Decoding: return "Reading model";
    case AssetLoadState::ReadyForUpload: return "Waiting for upload";
    case AssetLoadState::Uploading: return "Uploading";
    case AssetLoadState::Ready: return "Ready";
    case AssetLoadState::Failed: return "Failed";
    case AssetLoadState::Cancelled: return "Cancelled";
    }
    return "Unknown";
}
