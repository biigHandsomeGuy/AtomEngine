#pragma once

#include "CommandListManager.h"

class CommandContext;

class ContextManager
{
public:
    ContextManager() {}

    CommandContext* AllocateContext(D3D12_COMMAND_LIST_TYPE Type);

    void FreeContext(CommandContext* Context);
    void DestroyAllContexts();
private:
    std::vector<std::unique_ptr<CommandContext>> sm_ContextPool[4];
    // one for each command list type
    std::queue<CommandContext*> sm_AvailableContext[4];
    std::mutex sm_ContextAllocationMutex;
};

struct NonCopyable
{
    NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

class CommandContext : NonCopyable
{
    friend class ContextManager;
private:
    CommandContext(D3D12_COMMAND_LIST_TYPE Type);

    void Reset(void);
public:
    ID3D12GraphicsCommandList* m_CommandList;
    static CommandContext& Begin(const std::wstring ID = L"");

    uint64_t Finish(bool WaitForCompletion = false);

    // Prepare to render by reserving a command list and command allocator
    void Initialize();

protected:
    CommandListManager* m_OwningManager;
    
    ID3D12CommandAllocator* m_CurrentAllocator;

    std::wstring m_ID;
    void SetID(const std::wstring& ID) { m_ID = ID; }

    D3D12_COMMAND_LIST_TYPE m_Type;
};