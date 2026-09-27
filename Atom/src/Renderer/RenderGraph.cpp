#include "pch.h"
#include "RenderGraph.h"

#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

void RenderGraph::AddPass(std::wstring name, std::initializer_list<ResourceUse> resources, PassCallback callback)
{
    m_Passes.push_back({ std::move(name), std::vector<ResourceUse>(resources), std::move(callback) });
    m_ExecutionOrder.clear();
}

bool RenderGraph::Compile(std::string* error)
{
    struct ResourceHistory
    {
        size_t LastWriter = static_cast<size_t>(-1);
        std::vector<size_t> Readers;
    };

    const size_t passCount = m_Passes.size();
    std::vector<std::unordered_set<size_t>> edges(passCount);
    std::vector<size_t> indegree(passCount, 0);
    std::unordered_map<GpuResource*, ResourceHistory> history;

    auto addDependency = [&](size_t before, size_t after)
    {
        if (before != static_cast<size_t>(-1) && before != after && edges[before].insert(after).second)
            ++indegree[after];
    };

    for (size_t passIndex = 0; passIndex < passCount; ++passIndex)
    {
        std::unordered_set<GpuResource*> usedInPass;
        for (const ResourceUse& use : m_Passes[passIndex].Resources)
        {
            if (use.Resource == nullptr)
            {
                if (error) *error = "Render graph pass declares a null resource.";
                m_ExecutionOrder.clear();
                return false;
            }
            if (!usedInPass.insert(use.Resource).second)
            {
                if (error) *error = "A render graph pass declares the same resource more than once.";
                m_ExecutionOrder.clear();
                return false;
            }

            auto& resourceHistory = history[use.Resource];
            if (use.Mode == Access::Read)
            {
                addDependency(resourceHistory.LastWriter, passIndex);
                resourceHistory.Readers.push_back(passIndex);
            }
            else
            {
                addDependency(resourceHistory.LastWriter, passIndex);
                for (size_t reader : resourceHistory.Readers)
                    addDependency(reader, passIndex);
                resourceHistory.Readers.clear();
                resourceHistory.LastWriter = passIndex;
            }
        }
    }

    std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready;
    for (size_t i = 0; i < passCount; ++i)
        if (indegree[i] == 0)
            ready.push(i);

    m_ExecutionOrder.clear();
    while (!ready.empty())
    {
        const size_t current = ready.top();
        ready.pop();
        m_ExecutionOrder.push_back(current);
        for (size_t next : edges[current])
            if (--indegree[next] == 0)
                ready.push(next);
    }

    if (m_ExecutionOrder.size() != passCount)
    {
        if (error) *error = "Render graph contains a dependency cycle.";
        m_ExecutionOrder.clear();
        return false;
    }
    return true;
}

void RenderGraph::Execute(CommandContext& context)
{
    std::string error;
    if (m_ExecutionOrder.size() != m_Passes.size() && !Compile(&error))
        throw std::logic_error("Cannot execute render graph: " + error);

    for (size_t passIndex : m_ExecutionOrder)
    {
        Pass& pass = m_Passes[passIndex];
        for (const ResourceUse& use : pass.Resources)
            context.TransitionResource(*use.Resource, use.State);
        context.FlushResourceBarriers();
        if (pass.Callback)
            pass.Callback(context);
    }
}

void RenderGraph::Clear()
{
    m_Passes.clear();
    m_ExecutionOrder.clear();
}
