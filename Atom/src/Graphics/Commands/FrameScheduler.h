#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

class CommandListManager;

// Render-thread service. BeginFrame waits only for reuse of the oldest slot;
// EndFrame records a direct-queue fence AFTER all consumers and presentation.
// Call Drain before destroying resources/device state owned by the application.
class FrameScheduler
{
public:
    static constexpr uint32_t FramesInFlight = 3;
    struct Statistics
    {
        uint64_t Submissions = 0;
        uint64_t SlotReuses = 0;
        uint64_t ThrottleWaits = 0;
        uint64_t Drains = 0;
        uint32_t PeakInFlight = 0;
    };

    explicit FrameScheduler(CommandListManager& queues) : m_Queues(queues) {}
    FrameScheduler(const FrameScheduler&) = delete;
    FrameScheduler& operator=(const FrameScheduler&) = delete;

    uint32_t BeginFrame();
    void Retain(std::shared_ptr<const void> resource);
    uint64_t EndFrame();
    void Drain();
    uint32_t PendingFrames() const;
    size_t RetainedResources() const;
    bool FrameOpen() const { return m_FrameOpen; }
    const Statistics& Stats() const { return m_Stats; }

private:
    struct Slot
    {
        uint64_t Fence = 0;
        std::vector<std::shared_ptr<const void>> Resources;
        std::unordered_set<const void*> ResourceIds;
    };
    CommandListManager& m_Queues;
    std::array<Slot, FramesInFlight> m_Slots;
    Statistics m_Stats;
    uint32_t m_CurrentSlot = 0;
    bool m_FrameOpen = false;
};
