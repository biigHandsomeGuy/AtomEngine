#pragma once


class DescriptorAllocator
{
public:
	DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE Type)
		:m_Type(Type), m_CurrentType(nullptr), m_DescriptorSize(0)
	{
		m_CurrentHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
	}

protected:
	static const uint32_t sm_NumDescriptorPerHeap = 256;
	static std::mutex sm_AllocationMutex;
	static std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> sm_DescriptorHeapPool;


	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	ID3D12DescriptorHeap* m_CurrentType;
	D3D12_CPU_DESCRIPTOR_HANDLE m_CurrentHandle;

	uint32_t m_DescriptorSize;
	uint32_t m_RemainFreeHandles;
};

class DescriptorHandle
{
public:
	DescriptorHandle()
	{
		m_CpuHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		m_GpuHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
	}

	DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle)
		: m_CpuHandle(CpuHandle), m_GpuHandle(GpuHandle) {}

	DescriptorHandle operator+(UINT Offset) const
	{
		DescriptorHandle ret = *this;
		ret += Offset;
		return ret;
	}

	void operator+=(UINT Offset)
	{
		if (m_CpuHandle.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
			m_CpuHandle.ptr += Offset;
		if (m_GpuHandle.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
			m_GpuHandle.ptr += Offset;
	}

	size_t GetCpuPtr() const
	{
		return m_CpuHandle.ptr;
	}
	size_t GetGpuPtr() const
	{
		return m_GpuHandle.ptr;
	}

private:
	D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE m_GpuHandle;
};


class DescriptorHeap
{
public:
	DescriptorHeap() {}
	~DescriptorHeap() { Destory(); }

	void Create(const std::wstring& DebugHeapName, D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32_t MaxCount);
	void Destory() { m_Heap = nullptr; }

	bool HasAvailableSpace(uint32_t Count) const
	{
		return Count <= m_NumFreeDescriptors;
	}

	DescriptorHandle Alloc(uint32_t Count = 1);

	DescriptorHandle operator[](uint32_t ArrayIndex) const
	{
		return m_FirstHandle + ArrayIndex * m_DescriptorSize;
	}

	uint32_t GetOffsetOfHandle(const DescriptorHandle& Handle) const
	{
		return (Handle.GetCpuPtr() - m_FirstHandle.GetCpuPtr()) / m_DescriptorSize;
	}

	bool ValidateHandle(const DescriptorHandle& Handle) const;

	ID3D12DescriptorHeap* GetHeapPointer() const
	{
		return m_Heap.Get();
	}

	uint32_t GetDescriptorSize() const
	{
		return m_DescriptorSize;
	}
private:
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_Heap;
	D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc;
	uint32_t m_DescriptorSize;
	uint32_t m_NumFreeDescriptors;
	DescriptorHandle m_FirstHandle;
	DescriptorHandle m_NextFreeHandle;
};