#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include "CommandContext.h"

// An ordered set of GPU passes with declared resource access. The first read of
// a resource treats it as imported (created or produced outside this graph).
// The graph derives write-after-read/write-after-write dependencies and applies
// the declared D3D12 state before each pass executes.
class RenderGraph
{
public:
    enum class Access
    {
        Read,
        Write,
        ReadWrite
    };

    struct ResourceUse
    {
        GpuResource* Resource = nullptr;
        D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COMMON;
        Access Mode = Access::Read;
    };

    using PassCallback = std::function<void(CommandContext&)>;

    void AddPass(std::wstring name, std::initializer_list<ResourceUse> resources, PassCallback callback);
    bool Compile(std::string* error = nullptr);
    void Execute(CommandContext& context);
    void Clear();

private:
    struct Pass
    {
        std::wstring Name;
        std::vector<ResourceUse> Resources;
        PassCallback Callback;
    };

    std::vector<Pass> m_Passes;
    std::vector<size_t> m_ExecutionOrder;
};
