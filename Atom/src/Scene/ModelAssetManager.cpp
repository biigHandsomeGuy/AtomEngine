#include "pch.h"
#include "ModelAssetManager.h"

#include <chrono>
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

AssetRequestId ModelAssetManager::Request(const std::string& path)
{
    const std::string source = CanonicalPath(path);
    std::lock_guard lock(m_Mutex);
    if (m_Stopping) throw std::runtime_error("The asset manager has shut down.");
    if (auto found = m_PathRequests.find(source); found != m_PathRequests.end())
    {
        const auto& record = m_Records.at(found->second);
        if (!record->Cancelled.load() && record->Snapshot.State != AssetLoadState::Failed)
            return found->second;
    }
    auto record = std::make_shared<Record>();
    record->Snapshot.Id = m_NextId++;
    record->Snapshot.SourcePath = source;
    record->Snapshot.State = AssetLoadState::Queued;
    m_Records.emplace(record->Snapshot.Id, record);
    m_PathRequests[source] = record->Snapshot.Id;
    m_Queue.push_back(record);
    m_Changed.notify_all();
    return record->Snapshot.Id;
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
            if (record.Cancelled.load()) snapshot.Data = {};
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
