#include "pch.h"
#include "CommandContext.h"
#include "GraphicsCore.h"


using namespace Graphics;


CommandContext* ContextManager::AllocateContext(D3D12_COMMAND_LIST_TYPE Type)
{
	std::lock_guard<std::mutex> LockGurad(sm_ContextAllocationMutex);

	auto& AvailablContext = sm_AvailableContext[Type];

	CommandContext* ret = nullptr;
	if (AvailablContext.empty())
	{
		ret = new CommandContext(Type);
		sm_ContextPool[Type].emplace_back(ret);
		ret->Initialize();
	}
	else
	{
		ret = AvailablContext.front();
		AvailablContext.pop();
		ret->Reset();
	}

	assert(ret != nullptr);
	assert(ret->m_Type == Type);

	return ret;
}

void ContextManager::FreeContext(CommandContext* UsedContext)
{
	assert(UsedContext != nullptr);

	std::lock_guard<std::mutex> LockGurad(sm_ContextAllocationMutex);
	sm_AvailableContext[UsedContext->m_Type].push(UsedContext);
}

void ContextManager::DestroyAllContexts()
{
	for (uint32_t i = 0; i < 4; i++)
		sm_ContextPool[i].clear();
}



CommandContext::CommandContext(D3D12_COMMAND_LIST_TYPE Type)
	:m_Type(Type),
	m_OwningManager(nullptr),
	m_CommandList(nullptr),
	m_CurrentAllocator(nullptr)
{

}

void CommandContext::Reset(void)
{
	assert(m_CommandList != nullptr && m_CurrentAllocator == nullptr);

	m_CurrentAllocator = g_CommandManager.GetQueue(m_Type).RequestAllocator();
	m_CommandList->Reset(m_CurrentAllocator, nullptr);
}

CommandContext& CommandContext::Begin(const std::wstring ID)
{
	CommandContext* NewContext = g_ContextManager.AllocateContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	NewContext->SetID(ID);

	return *NewContext;
}

uint64_t CommandContext::Finish(bool WaitForCompletion)
{
	assert(m_Type == D3D12_COMMAND_LIST_TYPE_DIRECT || m_Type == D3D12_COMMAND_LIST_TYPE_COMPUTE);

	assert(m_CurrentAllocator != nullptr);

	CommandQueue& Queue = g_CommandManager.GetQueue(m_Type);

	uint64_t FenceValue = Queue.ExecuteCommandList(m_CommandList);
	Queue.DiscardAllocator(FenceValue, m_CurrentAllocator);
	m_CurrentAllocator = nullptr;
	if (WaitForCompletion)
		g_CommandManager.WaitForFence(FenceValue);

	g_ContextManager.FreeContext(this);
	
	return FenceValue;
}

void CommandContext::Initialize()
{
	g_CommandManager.CreateNewCommandList(m_Type, &m_CommandList, &m_CurrentAllocator);
}


