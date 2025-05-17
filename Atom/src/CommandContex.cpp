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
	m_CommandList(nullptr),
	m_CurrentAllocator(nullptr),
	m_CpuLinearAllocator(kCpuWritable),
	m_GpuLinearAllocator(kGpuExclusive)
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

void CommandContext::InitializeTexture(GpuResource& Dest, UINT NumSubresources, D3D12_SUBRESOURCE_DATA SubData[])
{
	UINT64 uploadBufferSize = GetRequiredIntermediateSize(Dest.GetResource(), 0, NumSubresources);

	CommandContext& InitContext = CommandContext::Begin();

	// copy data to the intermediate upload heap and then schedule a copy from the upload heap to the default texture
	DynAlloc mem = InitContext.ReserveUploadMemory(uploadBufferSize);
	UpdateSubresources(InitContext.m_CommandList, Dest.GetResource(), mem.Buffer.GetResource(), 0, 0, NumSubresources, SubData);
	InitContext.TransitionResource(Dest, D3D12_RESOURCE_STATE_GENERIC_READ);

	// Execute the command list and wait for it to finish so we can release the upload buffer
	InitContext.Finish(true);
}

void CommandContext::TransitionResource(GpuResource& Resource, D3D12_RESOURCE_STATES NewState, bool FlushImmediate)
{
	D3D12_RESOURCE_STATES OldState = Resource.m_UsageState;

	if (m_Type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
	{
		assert((OldState & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == OldState);
		assert((NewState & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == NewState);
	}

	if (OldState != NewState)
	{
		assert(m_NumBarriersToFlush < 16, "Exceeded arbitrary limit on buffered barriers");
		D3D12_RESOURCE_BARRIER& BarrierDesc = m_ResourceBarrierBuffer[m_NumBarriersToFlush++];

		BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		BarrierDesc.Transition.pResource = Resource.GetResource();
		BarrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		BarrierDesc.Transition.StateBefore = OldState;
		BarrierDesc.Transition.StateAfter = NewState;

		// Check to see if we already started the transition
		if (NewState == Resource.m_TransitioningState)
		{
			BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
			Resource.m_TransitioningState = (D3D12_RESOURCE_STATES)-1;
		}
		else
			BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

		Resource.m_UsageState = NewState;
	}
	else if (NewState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		InsertUAVBarrier(Resource, FlushImmediate);

	if (FlushImmediate || m_NumBarriersToFlush == 16)
		FlushResourceBarriers();
}
void CommandContext::InsertUAVBarrier(GpuResource& Resource, bool FlushImmediate)
{
	assert(m_NumBarriersToFlush < 16, "Exceeded arbitrary limit on buffered barriers");
	D3D12_RESOURCE_BARRIER& BarrierDesc = m_ResourceBarrierBuffer[m_NumBarriersToFlush++];

	BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	BarrierDesc.UAV.pResource = Resource.GetResource();

	if (FlushImmediate)
		FlushResourceBarriers();
}

