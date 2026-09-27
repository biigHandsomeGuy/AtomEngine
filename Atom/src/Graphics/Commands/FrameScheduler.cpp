#include "pch.h"
#include "FrameScheduler.h"

#include <algorithm>
#include <stdexcept>
#include "CommandListManager.h"

uint32_t FrameScheduler::BeginFrame()
{
    if (m_FrameOpen) throw std::logic_error("A frame is already open.");
    m_CurrentSlot = uint32_t(m_Stats.Submissions % FramesInFlight);
    auto& slot = m_Slots[m_CurrentSlot];
    if (m_Stats.Submissions >= FramesInFlight) ++m_Stats.SlotReuses;
    if (slot.Fence != 0 && !m_Queues.IsFenceComplete(slot.Fence))
    {
        ++m_Stats.ThrottleWaits;
        m_Queues.WaitForFence(slot.Fence);
    }
    // Owners and descriptors associated with this slot can now be reused.
    slot.Fence = 0;
    slot.Resources.clear();
    slot.ResourceIds.clear();
    m_FrameOpen = true;
    return m_CurrentSlot;
}

void FrameScheduler::Retain(std::shared_ptr<const void> resource)
{
    if (!m_FrameOpen) throw std::logic_error("Resources must be retained inside an open frame.");
    if (!resource) return;
    auto& slot = m_Slots[m_CurrentSlot];
    if (slot.ResourceIds.insert(resource.get()).second) slot.Resources.push_back(std::move(resource));
}

uint64_t FrameScheduler::EndFrame()
{
    if (!m_FrameOpen) throw std::logic_error("No frame is open.");
    auto& slot = m_Slots[m_CurrentSlot];
    slot.Fence = m_Queues.GetGraphicsQueue().IncrementFence();
    ++m_Stats.Submissions;
    m_FrameOpen = false;
    m_Stats.PeakInFlight = std::max(m_Stats.PeakInFlight, PendingFrames());
    return slot.Fence;
}

void FrameScheduler::Drain()
{
    if (m_FrameOpen) throw std::logic_error("End the open frame before draining.");
    for (auto& slot : m_Slots)
    {
        if (slot.Fence != 0) m_Queues.WaitForFence(slot.Fence);
        slot.Fence = 0;
        slot.Resources.clear();
        slot.ResourceIds.clear();
    }
    ++m_Stats.Drains;
}

uint32_t FrameScheduler::PendingFrames() const
{
    uint32_t count = 0;
    for (const auto& slot : m_Slots)
        if (slot.Fence != 0 && !m_Queues.IsFenceComplete(slot.Fence)) ++count;
    return count;
}

size_t FrameScheduler::RetainedResources() const
{
    size_t count = 0;
    for (const auto& slot : m_Slots) count += slot.Resources.size();
    return count;
}
