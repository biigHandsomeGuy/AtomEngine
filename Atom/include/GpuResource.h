#pragma once


class GpuResource
{
public:
	GpuResource()
		:m_GpuVirtualAddress(D3D12_GPU_VIRTUAL_ADDRESS_NULL),
		m_UsageState(D3D12_RESOURCE_STATE_COMMON),
		m_TransitioningState((D3D12_RESOURCE_STATES)-1)
	{
	}

	GpuResource(ID3D12Resource* pResource, D3D12_RESOURCE_STATES CurrentState)
		:m_GpuVirtualAddress(D3D12_GPU_VIRTUAL_ADDRESS_NULL),
		m_pResource(pResource),
		m_UsageState(CurrentState),
		m_TransitioningState((D3D12_RESOURCE_STATES)-1)
	{
	}

	~GpuResource()
	{
		Destroy();
	}

	virtual void Destroy()
	{
		m_pResource = nullptr;
		m_GpuVirtualAddress = D3D12_GPU_VIRTUAL_ADDRESS_NULL;
		m_VersionID++;
	}
protected:
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pResource;
	D3D12_RESOURCE_STATES m_UsageState;
	D3D12_RESOURCE_STATES m_TransitioningState;
	D3D12_GPU_VIRTUAL_ADDRESS m_GpuVirtualAddress;

	// Used to identify when a resource changes so descriptors can be copied etc.
	uint32_t m_VersionID = 0;
};