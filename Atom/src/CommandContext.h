#pragma once

#include "CommandListManager.h"
#include "LinearAllocator.h"

#define VALID_COMPUTE_QUEUE_RESOURCE_STATES \
    ( D3D12_RESOURCE_STATE_UNORDERED_ACCESS \
    | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE \
    | D3D12_RESOURCE_STATE_COPY_DEST \
    | D3D12_RESOURCE_STATE_COPY_SOURCE )


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
	
	static CommandContext& Begin(const std::wstring ID = L"");

	uint64_t Finish(bool WaitForCompletion = false);
	uint64_t Flush(bool WaitForCompletion = false);

	// Prepare to render by reserving a command list and command allocator
	void Initialize();

	ID3D12GraphicsCommandList* GetCommandList() {
		return m_CommandList;
	}

	DynAlloc ReserveUploadMemory(size_t SizeInBytes)
	{
		return m_CpuLinearAllocator.Allocate(SizeInBytes);
	}

	static void InitializeTexture(GpuResource& Dest, UINT NumSubresources, D3D12_SUBRESOURCE_DATA SubData[]);
	void InsertUAVBarrier(GpuResource& Resource, bool FlushImmediate = false);
	inline void FlushResourceBarriers(void);

	void TransitionResource(GpuResource& Resource, D3D12_RESOURCE_STATES NewState, bool FlushImmediate = false);

protected:
	ID3D12GraphicsCommandList* m_CommandList;
	ID3D12CommandAllocator* m_CurrentAllocator;

	D3D12_RESOURCE_BARRIER m_ResourceBarrierBuffer[16];
	UINT m_NumBarriersToFlush = 0;

	LinearAllocator m_CpuLinearAllocator;
	LinearAllocator m_GpuLinearAllocator;

	std::wstring m_ID;
	void SetID(const std::wstring& ID) { m_ID = ID; }

	D3D12_COMMAND_LIST_TYPE m_Type;
};


inline void CommandContext::FlushResourceBarriers(void)
{
	if (m_NumBarriersToFlush > 0)
	{
		m_CommandList->ResourceBarrier(m_NumBarriersToFlush, m_ResourceBarrierBuffer);
		m_NumBarriersToFlush = 0;
	}
}