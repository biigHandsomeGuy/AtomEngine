#include "pch.h"

#include "DescriptorHeap.h"
#include "GraphicsCore.h"

using namespace Graphics;

void DescriptorHeap::Create(const std::wstring& DebugHeapName, D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32_t MaxCount)
{
	m_HeapDesc.Type = Type;
	m_HeapDesc.NumDescriptors = MaxCount;
	m_HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	m_HeapDesc.NodeMask = 0;

	ThrowIfFailed(g_Device->CreateDescriptorHeap(&m_HeapDesc, IID_PPV_ARGS(m_Heap.ReleaseAndGetAddressOf())));

#if defined(DEBUG) || defined(_DEBUG)
	m_Heap->SetName(DebugHeapName.c_str());
#else
	// hahaha
#endif
	m_DescriptorSize = g_Device->GetDescriptorHandleIncrementSize(Type);
	m_NumFreeDescriptors = MaxCount;
	m_FirstHandle = DescriptorHandle(
		m_Heap->GetCPUDescriptorHandleForHeapStart(),
		m_Heap->GetGPUDescriptorHandleForHeapStart()
	);
	m_NextFreeHandle = m_FirstHandle;
}

DescriptorHandle DescriptorHeap::Alloc(uint32_t Count)
{
	// if this failed, indicate the descriptor out of space, increase the heap size
	assert(HasAvailableSpace(Count));

	DescriptorHandle ret = m_NextFreeHandle;
	m_NextFreeHandle += m_DescriptorSize * Count;
	m_NumFreeDescriptors -= Count;

	return DescriptorHandle();
}

bool DescriptorHeap::ValidateHandle(const DescriptorHandle& Handle) const
{
	if (Handle.GetCpuPtr() - m_FirstHandle.GetCpuPtr() !=
		Handle.GetGpuPtr() - m_FirstHandle.GetGpuPtr())
		return false;

	if (Handle.GetCpuPtr() < m_FirstHandle.GetCpuPtr() ||
		Handle.GetCpuPtr() >= m_FirstHandle.GetCpuPtr() + m_HeapDesc.NumDescriptors * m_DescriptorSize)
		return false;


	return true;
}
