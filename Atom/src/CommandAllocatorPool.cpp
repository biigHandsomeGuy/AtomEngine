#include "pch.h"
#include "CommandAllocatorPool.h"

CommandAllocatorPool::CommandAllocatorPool(D3D12_COMMAND_LIST_TYPE type)
	: m_cCommandListType(type), m_Device(nullptr)
{
}

CommandAllocatorPool::~CommandAllocatorPool()
{
	Shutdown();
}

void CommandAllocatorPool::Create(ID3D12Device* pDevice)
{
	m_Device = pDevice;
}

void CommandAllocatorPool::Shutdown()
{
	for (size_t i = 0; i < m_AllocatorPool.size(); ++i)
	{
		//m_AllocatorPool[i]->Release();
	}

	m_AllocatorPool.clear();
}

ID3D12CommandAllocator* CommandAllocatorPool::RequestAllocator(uint64_t CompletedFenceValue)
{
	std::lock_guard<std::mutex> LockGuard(m_AllocatorMutex);

	ID3D12CommandAllocator* pAllocator = nullptr;

	if (m_ReadyAllocators.empty() == false)
	{
		std::pair<uint64_t, ID3D12CommandAllocator*>& AllocatorPair = m_ReadyAllocators.front();

		if (AllocatorPair.first <= CompletedFenceValue)
		{
			pAllocator = AllocatorPair.second;
			ThrowIfFailed(pAllocator->Reset());
			m_ReadyAllocators.pop();
		}
	}

	// if m_ReadyAllocators is empty create a new one
	if (pAllocator == nullptr)
	{
		ThrowIfFailed(m_Device->CreateCommandAllocator(m_cCommandListType, IID_PPV_ARGS(&pAllocator)));

		wchar_t AllocatorName[32];
		swprintf(AllocatorName, 32, L"CommandAllocator %zu", m_AllocatorPool.size());
		pAllocator->SetName(AllocatorName);
		m_AllocatorPool.push_back(pAllocator);
	}

	return pAllocator;
}

void CommandAllocatorPool::DiscardAllocator(uint64_t FenceValue, ID3D12CommandAllocator* Allocator)
{
	std::lock_guard<std::mutex> LockGuard(m_AllocatorMutex);

	// That fence value indicates we are free to reset the allocator
	m_ReadyAllocators.push(std::make_pair(FenceValue, Allocator));
}
